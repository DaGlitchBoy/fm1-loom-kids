/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KIDS_FLASH_STORE_H
#define KIDS_FLASH_STORE_H
#include <stdint.h>

/* The contiguous LOOM-8 sample window. A device using this journal must disable
 * sample-bank writes and attachment before enabling these records. */
#define KFS_BASE 0xC1000u
#define KFS_LIMIT 0xDC000u
#define KFS_SECTOR 4096u
#define KFS_SONG_COPY (4u * KFS_SECTOR)
#define KFS_HEADER 20u
#define KFS_SONG_MAX (KFS_SONG_COPY - KFS_HEADER)
enum { KFS_SONG1, KFS_SONG2, KFS_SONG3, KFS_METADATA, KFS_RECORDS };

typedef struct {
    void *ctx;
    /* Read views stay valid until those sectors are changed. */
    const uint8_t *(*read)(void *ctx, uint32_t offset, uint32_t len);
    int (*erase)(void *ctx, uint32_t sector);
    int (*program)(void *ctx, uint32_t offset, const uint8_t *bytes, uint32_t len);
} kfs_io_t;

typedef struct {
    const uint8_t *data;
    uint32_t size, revision;
    unsigned copy;
} kfs_record_t;

typedef struct {
    const kfs_io_t *io;
    uint32_t offset, revision, size, used, crc;
    unsigned record;
    uint8_t page[128];
} kfs_write_t;

/* A zero revision denotes an empty record. A valid zero-size record can serve
 * as a tombstone. The returned memory is valid until this copy is erased. */
int kfs_read(const kfs_io_t *io, unsigned record, kfs_record_t *value);
/* Compare revisions before any erase. Returns -2 on revision conflict. */
int kfs_begin(kfs_write_t *write, const kfs_io_t *io, unsigned record, uint32_t expected, uint32_t size);
/* The chunk can point to read-only storage. Programming uses a small RAM page buffer. */
int kfs_append(kfs_write_t *write, const uint8_t *bytes, uint32_t len);
int kfs_commit(kfs_write_t *write);
#endif
