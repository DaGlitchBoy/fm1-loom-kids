/* SPDX-License-Identifier: MPL-2.0 */
/* Device shelf uses mapped song bytes, with a small combined metadata record. */
enum { KFS_META_BYTES = 9 + sizeof kpix_bank };

static ks_blob_t *ks_flash_view(const kfs_record_t *record)
{
    if (!record->size) return NULL;
    ks_blob_t *view = malloc(sizeof *view);
    if (view) *view = (ks_blob_t){.len = record->size, .refs = 1, .data = (uint8_t *)record->data};
    return view;
}

static int ks_device_read(int slot, uint32_t *revision, ks_blob_t **out)
{
    kfs_record_t record;
    *out = NULL;
    *revision = 0;
    if (kfs_read(&kfs_plat_io, slot < KS_SLOTS ? (unsigned)slot : KFS_METADATA, &record)) return -1;
    if (slot < KS_SLOTS) {
        *revision = record.revision;
        *out = ks_flash_view(&record);
        return record.size && !*out ? -1 : 0;
    }
    if (!record.revision) return 0;
    if (record.size != KFS_META_BYTES) return -1;
    uint32_t size = slot == KS_STARTUP ? 1u : KFS_META_BYTES - 1u;
    ks_blob_t *blob = ks_blob(size);
    if (!blob) return -1;
    memcpy(blob->data, record.data + (slot == KS_STARTUP ? 0 : 1), size);
    if (slot == KS_PIXELS && !get32(blob->data)) {
        ks_drop(blob);
        return 0;
    }
    *out = blob;
    return 0;
}

static int ks_file_write(int slot, uint32_t expected, const ks_blob_t *blob)
{
    kfs_write_t write;
    uint32_t size = blob ? blob->len : 0;
    const uint8_t *data = blob ? blob->data : NULL;
    uint8_t metadata[KFS_META_BYTES] = {0};
    if (slot >= KS_SLOTS) {
        kfs_record_t previous;
        if (kfs_read(&kfs_plat_io, KFS_METADATA, &previous)) return -1;
        if (previous.size) {
            if (previous.size != sizeof metadata) return -1;
            memcpy(metadata, previous.data, sizeof metadata);
        }
        if (slot == KS_STARTUP && size == 1) metadata[0] = data[0];
        else if (slot == KS_PIXELS && size == sizeof metadata - 1u) memcpy(metadata + 1, data, size);
        else return -1;
        slot = KFS_METADATA;
        expected = previous.revision;
        data = metadata;
        size = sizeof metadata;
    }
    int rc = kfs_begin(&write, &kfs_plat_io, (unsigned)slot, expected, size);
    if (rc) return rc;
    return kfs_append(&write, data, size) || kfs_commit(&write) ? -1 : 0;
}
