/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kids_flash_store.h"

enum { IMAGE_BYTES = KFS_LIMIT - KFS_BASE + 2 * KFS_SECTOR };
static uint8_t image[IMAGE_BYTES], snapshot[IMAGE_BYTES];
static uint8_t prior[1000], next[KFS_SONG_MAX];
static unsigned operations, fail_at, surviving_bytes;

static uint8_t *address(uint32_t offset, uint32_t len)
{
    if (offset < KFS_BASE || offset > KFS_LIMIT || len > KFS_LIMIT - offset) return NULL;
    return image + KFS_SECTOR + offset - KFS_BASE;
}

static const uint8_t *read_bytes(void *ctx, uint32_t offset, uint32_t len)
{
    (void)ctx;
    return address(offset, len);
}

static int erase(void *ctx, uint32_t offset)
{
    (void)ctx;
    uint8_t *p = address(offset, KFS_SECTOR);
    assert(p && offset % KFS_SECTOR == 0);
    int fail = ++operations == fail_at;
    memset(p, 0xff, fail ? KFS_SECTOR / 2 : KFS_SECTOR);
    return fail ? -1 : 0;
}

static int program(void *ctx, uint32_t offset, const uint8_t *bytes, uint32_t len)
{
    (void)ctx;
    uint8_t *p = address(offset, len);
    assert(p && len <= 128 && bytes);
    int fail = ++operations == fail_at;
    if (fail && len > surviving_bytes) len = surviving_bytes;
    for (uint32_t i = 0; i < len; i++) {
        assert((p[i] & bytes[i]) == bytes[i]);
        p[i] &= bytes[i];
    }
    return fail ? -1 : 0;
}

static const kfs_io_t io = {NULL, read_bytes, erase, program};

static int write_record(unsigned record, uint32_t revision, const uint8_t *bytes, uint32_t size)
{
    kfs_write_t write;
    int rc = kfs_begin(&write, &io, record, revision, size);
    if (rc) return rc;
    /* Model the small pieces delivered by a streaming compressor. */
    for (uint32_t pos = 0; pos < size;) {
        uint32_t n = size - pos > 17 ? 17 : size - pos;
        if (kfs_append(&write, bytes + pos, n)) return -1;
        pos += n;
    }
    return kfs_commit(&write);
}

static kfs_record_t check(unsigned record, uint32_t revision, const uint8_t *bytes, uint32_t size)
{
    kfs_record_t value;
    assert(kfs_read(&io, record, &value) == 0);
    assert(value.revision == revision && value.size == size);
    if (size) assert(memcmp(value.data, bytes, size) == 0);
    return value;
}

int main(void)
{
    memset(image, 0xa5, sizeof image);
    memset(address(KFS_BASE, KFS_LIMIT - KFS_BASE), 0xff, KFS_LIMIT - KFS_BASE);
    for (uint32_t i = 0; i < sizeof prior; i++) prior[i] = (uint8_t)(i * 31u);
    for (uint32_t i = 0; i < sizeof next; i++) next[i] = (uint8_t)(i * 29u);
    check(KFS_SONG2, 0, NULL, 0);
    assert(write_record(KFS_SONG1, 0, prior, sizeof prior) == 0);
    assert(write_record(KFS_SONG2, 0, prior, sizeof prior) == 0);
    assert(write_record(KFS_SONG3, 0, prior, sizeof prior) == 0);
    assert(write_record(KFS_METADATA, 0, prior, sizeof prior) == 0);
    memcpy(snapshot, image, sizeof image);
    operations = 0;
    assert(write_record(KFS_SONG2, 0, next, sizeof next) == -2);
    assert(write_record(KFS_SONG2, 1, next, sizeof next + 1) == -1);
    assert(!operations && !memcmp(image, snapshot, sizeof image));
    assert(write_record(KFS_SONG2, 1, next, sizeof next) == 0);
    unsigned total = operations;
    check(KFS_SONG2, 2, next, sizeof next);
    for (unsigned op = 1; op <= total; op++) {
        memcpy(image, snapshot, sizeof image);
        operations = 0;
        fail_at = op;
        surviving_bytes = 2;
        assert(write_record(KFS_SONG2, 1, next, sizeof next) == -1);
        check(KFS_SONG2, 1, prior, sizeof prior);
        check(KFS_SONG1, 1, prior, sizeof prior);
        check(KFS_SONG3, 1, prior, sizeof prior);
        check(KFS_METADATA, 1, prior, sizeof prior);
    }
    memcpy(image, snapshot, sizeof image);
    operations = 0;
    fail_at = total;
    surviving_bytes = 4;
    assert(write_record(KFS_SONG2, 1, next, sizeof next) == 0);
    kfs_record_t newest = check(KFS_SONG2, 2, next, sizeof next);
    image[(size_t)(newest.data - image) + 200] ^= 1u;
    check(KFS_SONG2, 1, prior, sizeof prior);
    fail_at = 0;
    assert(write_record(KFS_SONG2, 1, NULL, 0) == 0);
    check(KFS_SONG2, 2, NULL, 0);
    assert(!memcmp(image, snapshot, KFS_SECTOR));
    assert(!memcmp(image + IMAGE_BYTES - 2 * KFS_SECTOR,
                   snapshot + IMAGE_BYTES - 2 * KFS_SECTOR, 2 * KFS_SECTOR));
    printf("Simulated %u interrupted overwrite operations with the previous song intact\n", total);
}
