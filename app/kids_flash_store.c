/* SPDX-License-Identifier: MPL-2.0 */
#include "kids_flash_store.h"

#define KFS_MAGIC 0x3146534cu /* LSF1 */
#define KFS_METADATA_BASE (KFS_BASE + 6u * KFS_SONG_COPY)

_Static_assert(KFS_BASE % KFS_SECTOR == 0 && KFS_LIMIT % KFS_SECTOR == 0, "aligned journal");
_Static_assert(KFS_METADATA_BASE + 2u * KFS_SECTOR <= KFS_LIMIT, "journal confined to sample window");

static uint32_t kfs_get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void kfs_put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8u * i));
}

static uint32_t kfs_crc(uint32_t crc, const uint8_t *p, uint32_t len)
{
    while (len--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return crc;
}

static uint32_t kfs_copy_size(unsigned record)
{
    return record == KFS_METADATA ? KFS_SECTOR : KFS_SONG_COPY;
}

static uint32_t kfs_offset(unsigned record, unsigned copy)
{
    if (record == KFS_METADATA) return KFS_METADATA_BASE + copy * KFS_SECTOR;
    return KFS_BASE + (2u * record + copy) * KFS_SONG_COPY;
}

int kfs_read(const kfs_io_t *io, unsigned record, kfs_record_t *value)
{
    if (record >= KFS_RECORDS) return -1;
    *value = (kfs_record_t){.data = 0};
    for (unsigned copy = 0; copy < 2; copy++) {
        uint32_t offset = kfs_offset(record, copy);
        const uint8_t *head = io->read(io->ctx, offset, KFS_HEADER);
        if (!head) return -1;
        if (kfs_get32(head) != KFS_MAGIC || kfs_get32(head + 4) != record) continue;
        uint32_t rev = kfs_get32(head + 8), len = kfs_get32(head + 12);
        if (rev <= value->revision || len > kfs_copy_size(record) - KFS_HEADER) continue;
        const uint8_t *data = io->read(io->ctx, offset + KFS_HEADER, len);
        if (!data) return -1;
        if (kfs_crc(kfs_crc(UINT32_MAX, head + 4, 12), data, len) != kfs_get32(head + 16))
            continue;
        *value = (kfs_record_t){data, len, rev, copy};
    }
    return 0;
}

int kfs_begin(kfs_write_t *write, const kfs_io_t *io, unsigned record, uint32_t expected, uint32_t size)
{
    write->io = 0;
    if (record >= KFS_RECORDS || size > kfs_copy_size(record) - KFS_HEADER || expected == UINT32_MAX)
        return -1;
    kfs_record_t old;
    if (kfs_read(io, record, &old)) return -1;
    if (old.revision != expected) return -2;
    uint32_t offset = kfs_offset(record, old.revision ? old.copy ^ 1u : 0);
    for (uint32_t sector = 0; sector < kfs_copy_size(record); sector += KFS_SECTOR)
        if (io->erase(io->ctx, offset + sector)) return -1;
    uint8_t head[12];
    kfs_put32(head, record);
    kfs_put32(head + 4, expected + 1u);
    kfs_put32(head + 8, size);
    write->io = io;
    write->offset = offset;
    write->revision = expected + 1u;
    write->size = size;
    write->used = 0;
    write->crc = kfs_crc(UINT32_MAX, head, sizeof head);
    write->record = record;
    return 0;
}

int kfs_append(kfs_write_t *write, const uint8_t *bytes, uint32_t len)
{
    if (!write->io || len > write->size - write->used) return -1;
    const kfs_io_t *io = write->io;
    while (len) {
        uint32_t pos = write->used % sizeof write->page;
        uint32_t n = sizeof write->page - pos;
        if (n > len) n = len;
        for (uint32_t i = 0; i < n; i++) write->page[pos + i] = bytes[i];
        if (pos + n == sizeof write->page &&
            io->program(io->ctx, write->offset + KFS_HEADER + write->used - pos, write->page, sizeof write->page)) {
            write->io = 0;
            return -1;
        }
        write->crc = kfs_crc(write->crc, write->page + pos, n);
        write->used += n;
        bytes += n;
        len -= n;
    }
    return 0;
}

int kfs_commit(kfs_write_t *write)
{
    if (!write->io || write->used != write->size) return -1;
    const kfs_io_t *io = write->io;
    write->io = 0;
    uint32_t tail = write->used % sizeof write->page;
    if (tail && io->program(io->ctx, write->offset + KFS_HEADER + write->used - tail, write->page, tail))
        return -1;
    uint8_t head[KFS_HEADER];
    kfs_put32(head, KFS_MAGIC);
    kfs_put32(head + 4, write->record);
    kfs_put32(head + 8, write->revision);
    kfs_put32(head + 12, write->size);
    kfs_put32(head + 16, write->crc);
    if (io->program(io->ctx, write->offset + 4, head + 4, sizeof head - 4)) return -1;
    const uint8_t *stored = io->read(io->ctx, write->offset, KFS_HEADER + write->size);
    if (!stored || kfs_get32(stored) != UINT32_MAX) return -1;
    for (unsigned i = 4; i < sizeof head; i++) if (head[i] != stored[i]) return -1;
    if (kfs_crc(kfs_crc(UINT32_MAX, stored + 4, 12), stored + KFS_HEADER, write->size) != write->crc)
        return -1;
    /* A completed marker defines the commit even if the driver reports an error
     * after programming it. Validate the record in either case. */
    io->program(io->ctx, write->offset, head, 4);
    kfs_record_t latest;
    return kfs_read(io, write->record, &latest) || latest.revision != write->revision ? -1 : 0;
}
