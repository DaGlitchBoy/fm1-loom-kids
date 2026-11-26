/* SPDX-License-Identifier: MPL-2.0 */
/* Included after the storage API and song encoder in the application unity
 * build. The host exercises this adapter against its simulated NOR flash. */
#include "kids_flash_store.c"

_Static_assert(KFS_BASE == SMP_REG0 && KFS_LIMIT == SMP_REG0 + SMP_REG0_SIZE,
               "Kids journal uses only the designated sample window");
_Static_assert(KFS_BASE >= APP_RES_BASE + APP_RES_SECTORS * KFS_SECTOR,
               "Kids journal stays above factory resources and Studio projects");
_Static_assert(KFS_LIMIT <= PLAT_DATA_HI, "Kids journal stays below updater staging");

static const uint8_t *kfs_plat_read(void *ctx, uint32_t offset, uint32_t len)
{
    (void)ctx;
    if (offset < KFS_BASE || offset >= KFS_LIMIT || len > KFS_LIMIT - offset) return NULL;
    return plat_data(offset);
}

static int kfs_plat_erase(void *ctx, uint32_t offset)
{
    (void)ctx;
    if (offset < KFS_BASE || offset > KFS_LIMIT - KFS_SECTOR) return -1;
    return plat_data_erase(offset);
}

static int kfs_plat_program(void *ctx, uint32_t offset, const uint8_t *bytes, uint32_t len)
{
    (void)ctx;
    if (offset < KFS_BASE || offset >= KFS_LIMIT || len > KFS_LIMIT - offset) return -1;
    return plat_data_write(offset, bytes, len);
}

static const kfs_io_t kfs_plat_io = {NULL, kfs_plat_read, kfs_plat_erase, kfs_plat_program};

#define KS_FLASH_BLOCK 4096u

typedef struct {
    unsigned record;
    uint32_t revision;
    kfs_write_t write;
} ks_flash_output_t;

static int ks_flash_begin(void *ctx, uint32_t size)
{
    ks_flash_output_t *out = ctx;
    return kfs_begin(&out->write, &kfs_plat_io, out->record, out->revision, size);
}

static int ks_flash_put(void *ctx, const uint8_t *p, uint32_t n)
{
    ks_flash_output_t *out = ctx;
    return kfs_append(&out->write, p, n);
}

static int ks_flash_save(unsigned record, uint32_t revision)
{
    if (record >= KS_SLOTS) return -1;
    ks_flash_output_t out = {.record = record, .revision = revision};
    lz_sink_t sink = {ks_flash_put, &out};
    if (ks_stream_song(&sink, ks_flash_begin, KS_FLASH_BLOCK)) return -1;
    return kfs_commit(&out.write);
}

typedef struct {
    set_in_t input;
    lz_src_t source;
    uint8_t *scratch;
    lz_sink_t *target;
    uint32_t left, crc;
    int packed;
} ks_flash_reader_t;

static int ks_flash_part(ks_flash_reader_t *r, uint32_t size)
{
    if (size > r->left) return -1;
    r->left -= size;
    while (size) {
        uint32_t n = size > KS_FLASH_BLOCK ? KS_FLASH_BLOCK : size;
        if (r->packed) {
            if (lz_decompress(&r->source, r->scratch, n)) return -1;
        } else {
            if (n > r->input.left) return -1;
            memcpy(r->scratch, r->input.p, n);
            r->input.p += n;
            r->input.left -= n;
        }
        r->crc = crc32_add(r->crc, r->scratch, n);
        if (r->target && r->target->put(r->target->ctx, r->scratch, n)) return -1;
        size -= n;
    }
    return 0;
}

/* The journal must validate the encoded record before this pass. A null
 * target verifies the complete raw stream before a caller supplies a sink. */
static int ks_flash_read_song(const kfs_record_t *record, uint8_t *scratch, lz_sink_t *target)
{
    const uint8_t *p = record->data;
    if (record->size < KS_HEADER || get32(p) != KS_MAGIC || get32(p + 4) != KS_VERSION ||
        get32(p + 12) != record->size - KS_HEADER || get32(p + 20) > 1 ||
        get32(p + KS_HEADER_V1) != crc32_of(p + KS_HEADER_V1 + 4, KPIX_BYTES + 1))
        return -1;
    ks_flash_reader_t r = {
        .input = {p + KS_HEADER, record->size - KS_HEADER},
        .scratch = scratch, .target = target, .left = get32(p + 8), .packed = (int)get32(p + 20)
    };
    r.source = (lz_src_t){set_get, &r.input};
    if (r.left < sizeof(ks_song_t) + NTRK * 4 || r.left > KS_RAW_MAX ||
        ks_flash_part(&r, 8) || ks_flash_part(&r, sizeof(settings_t) - 8) ||
        ks_flash_part(&r, sizeof(patterns_t)) || ks_flash_part(&r, sizeof(ks_meta_t)))
        return -1;
    for (int track = 0; track < NTRK; track++) {
        if (ks_flash_part(&r, 4)) return -1;
        uint32_t count = get32(scratch);
        if (!count) continue;
        if (count > NLANE + 1u || ks_flash_part(&r, sizeof pats.pat[0]) ||
            ks_flash_part(&r, (count - 1) * sizeof(lane_t))) return -1;
    }
    return r.left || r.input.left || r.crc != get32(p + 16) ? -1 : 0;
}
