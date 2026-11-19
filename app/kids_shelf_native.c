/* SPDX-License-Identifier: MPL-2.0 */
#ifndef PLAT_WEB
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#define KS_FILE_MAGIC 0x31464b4cu /* LKF1 */
static int ks_fail_next; /* host fault injection */

static int ks_file_path(char *dst, size_t size, int slot, const char *suffix)
{
    const char *dir = getenv("LOOM_KIDS_SHELF");
    if (!dir || !*dir) return 0;
    int n = slot == KS_PIXELS ? snprintf(dst, size, "%s/pixelmaps%s", dir, suffix)
                     : snprintf(dst, size, "%s/song-%d%s", dir, slot + 1, suffix);
    return n < 0 || (size_t)n >= size ? -1 : 1;
}

static int ks_file_read(const char *path, uint32_t *revision, ks_blob_t **out)
{
    *revision = 0;
    *out = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 0 : -1;
    uint8_t head[12];
    int rc = -1;
    ks_blob_t *blob = NULL;
    if (fread(head, 1, sizeof head, f) != sizeof head || get32(head) != KS_FILE_MAGIC) goto done;
    uint32_t rev = get32(head + 4), len = get32(head + 8);
    if (!rev || len > KS_BLOB_MAX) goto done;
    if (len) {
        blob = ks_blob(len);
        if (!blob || fread(blob->data, 1, len, f) != len) goto done;
    }
    if (fgetc(f) != EOF || ferror(f)) goto done;
    *revision = rev;
    *out = blob;
    blob = NULL;
    rc = 0;
done:
    ks_drop(blob);
    fclose(f);
    return rc;
}

static int ks_file_write(int slot, uint32_t expected, const ks_blob_t *blob)
{
    if (ks_fail_next) { ks_fail_next = 0; return -1; }
    char path[1024], lockpath[1024], temp[1024];
    int backend = ks_file_path(path, sizeof path, slot, ".ks");
    if (backend <= 0) return backend;
    if (ks_file_path(lockpath, sizeof lockpath, slot, ".lock") != 1 ||
        ks_file_path(temp, sizeof temp, slot, ".tmp.XXXXXX") != 1) return -1;
    int lock = open(lockpath, O_CREAT | O_RDWR, 0600);
    if (lock < 0) return -1;
    int rc = -1;
    if (flock(lock, LOCK_EX)) goto done;
    uint32_t current;
    ks_blob_t *prior;
    if (ks_file_read(path, &current, &prior)) goto done;
    ks_drop(prior);
    if (current != expected) { rc = -2; goto done; }

    int fd = mkstemp(temp);
    if (fd < 0) goto done;
    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(temp); goto done; }
    uint8_t head[12];
    put32(head, KS_FILE_MAGIC); put32(head + 4, expected + 1u);
    put32(head + 8, blob ? blob->len : 0);
    int ok = fwrite(head, 1, sizeof head, f) == sizeof head &&
        (!blob || fwrite(blob->data, 1, blob->len, f) == blob->len) &&
        !fflush(f) && !fsync(fd);
    if (fclose(f)) ok = 0;
    if (ok && !rename(temp, path)) {
        rc = 0;
        int dir = open(getenv("LOOM_KIDS_SHELF"), O_RDONLY);
        if (dir >= 0) { (void)fsync(dir); close(dir); }
    } else unlink(temp);
done:
    close(lock);
    return rc;
}
#endif
