/* SPDX-License-Identifier: MPL-2.0 */
/* Fixed arena for typed banks and edit deltas. All calls run in the UI loop. */
#define KIDS_HEAP_UNIT 64u
#define KIDS_HEAP_UNITS 640u
static uint64_t kids_heap[KIDS_HEAP_UNITS][KIDS_HEAP_UNIT / sizeof(uint64_t)] POOL;
static uint8_t kids_heap_used[KIDS_HEAP_UNITS];

static void *kids_malloc(size_t bytes)
{
    if (!bytes || bytes > sizeof kids_heap - sizeof(uint64_t)) return NULL;
    unsigned units = (unsigned)((bytes + sizeof(uint64_t) + KIDS_HEAP_UNIT - 1u) / KIDS_HEAP_UNIT);
    for (unsigned start = 0, run = 0; start < KIDS_HEAP_UNITS; start++) {
        run = kids_heap_used[start] ? 0 : run + 1u;
        if (run != units) continue;
        unsigned first = start + 1u - units;
        for (unsigned i = first; i <= start; i++) kids_heap_used[i] = 1;
        kids_heap[first][0] = units;
        return &kids_heap[first][1];
    }
    return NULL;
}

static void kids_free(void *p)
{
    if (!p) return;
    uint8_t *start = (uint8_t *)p - sizeof(uint64_t);
    unsigned first = (unsigned)(start - (uint8_t *)kids_heap) / KIDS_HEAP_UNIT;
    unsigned count = (unsigned)kids_heap[first][0];
    for (unsigned i = first; i < first + count; i++) kids_heap_used[i] = 0;
}

static void *kids_calloc(size_t n, size_t size)
{
    if (n && size > sizeof kids_heap / n) return NULL;
    void *p = kids_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}
#define malloc kids_malloc
#define calloc kids_calloc
#define free kids_free
