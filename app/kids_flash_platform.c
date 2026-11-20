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
    if (ks_stream_song(&sink, ks_flash_begin)) return -1;
    return kfs_commit(&out.write);
}
