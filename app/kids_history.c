/* SPDX-License-Identifier: MPL-2.0 */
/* Included by kids.c. Journal musical bytes, never transport cursors or screen focus.
 * Each entry owns a compact set of changed byte runs and, for an engine-type
 * change, the inactive alternate bank. Each changed run keeps both values so
 * a later audition cannot replace the recorded redo value. */
enum { KH_SMALL, KH_SOUND, KH_STEP, KH_RECORD, KH_NAME, KH_SLOT, KH_LOAD };
#define KH_LIMIT 10
#define KH_SWEEP_MS 350u
#define KH_HOLD_MS 250u
#define KH_CAPTURE_BYTES (sizeof pats.pat[0] + sizeof pats.lane + sizeof pats.hit + \
                          sizeof pats.scene + sizeof kids_kit_mutes + sizeof proj.trk[0] + 512u)
_Static_assert(KH_CAPTURE_BYTES < UINT16_MAX, "journal offsets fit in two bytes");

typedef struct {
    uint8_t *bytes;
    size_t size;
} kh_region_t;

typedef struct ks_blob ks_blob_t;
static void ks_drop(ks_blob_t *blob);

typedef struct {
    const char *name;
    uint16_t group;
    uint8_t scope, track, pattern, bank_changed, cell_changed;
    kids_bank_t *bank;
    kids_cell_t cell, cell_after;
    size_t size;
    uint8_t *runs;
    ks_blob_t *song_before, *song_after;
    uint8_t slot, was_dirty, after_dirty, before_slot, after_slot;
} kh_edit_t;

static int ks_replay(const kh_edit_t *e, int redo);

typedef struct {
    kh_edit_t edit;
    uint8_t before[KH_CAPTURE_BYTES];
    uint8_t active;
} kh_capture_t;

static kh_edit_t kh_entries[KH_LIMIT];
static kh_capture_t kh_edit, kh_take;
static uint8_t kh_count, kh_cursor, kh_merge, kh_memory_full;
static uint8_t kh_home, kh_visible;
static uint32_t kh_touched, kh_home_since;

static void kh_regions(const kh_edit_t *e, kh_region_t regions[32], int *count)
{
    int t = e->track, n = 0;
#define KH_REGION(obj) do { regions[n++] = (kh_region_t){(uint8_t *)&(obj), sizeof(obj)}; } while (0)
    if (e->scope == KH_NAME) {
        KH_REGION(kpix_song);
    } else if (e->scope == KH_RECORD) {
        for (int p = 0; p < NPATT; p++) KH_REGION(pats.pat[t][p].st);
    } else {
        KH_REGION(proj.trk[t]);
        KH_REGION(proj.tempo_x10); KH_REGION(proj.groove);
        KH_REGION(proj.trk[AUX_FX1].par[TP_E1]);
        KH_REGION(proj.trk[AUX_FX1].par[TP_E2]);
        KH_REGION(proj.trk[AUX_FX1].par[TP_E3]);
        KH_REGION(proj.trk[AUX_FX1].par[TP_E4]);
        KH_REGION(proj.trk[AUX_FX2].par[TP_E1]);
        KH_REGION(proj.trk[AUX_FX2].par[TP_E2]);
        KH_REGION(proj.trk[AUX_FX2].par[TP_E3]);
        KH_REGION(proj.trk[AUX_FX2].par[TP_E4]);
        KH_REGION(kids_fx_on[t]); KH_REGION(kids_fx_amount[t]);
        KH_REGION(kids_shaping[t]);
        KH_REGION(kids_arp_on[t]); KH_REGION(kids_arp_order[t]); KH_REGION(kids_arp_rate[t]);
        KH_REGION(kids_chord[t]); KH_REGION(kids_scale[t]);
        KH_REGION(kids_root); KH_REGION(ui.oct);
        KH_REGION(kids_metro); KH_REGION(kids_delay_choice);
        if (e->scope == KH_SOUND) {
            KH_REGION(kids_picks[t]);
            KH_REGION(pats.pat[t]);
            KH_REGION(pats.scene);
            KH_REGION(kids_kit_mutes);
        } else if (e->scope == KH_STEP) {
            KH_REGION(pats.pat[t][e->pattern]);
        } else {
            regions[n++] = (kh_region_t){&pats.pat[t][e->pattern].len, sizeof(tpat_t) - offsetof(tpat_t, len)};
        }
    }
    if (e->scope != KH_SMALL && e->scope != KH_NAME) {
        KH_REGION(pats.lane);
        KH_REGION(pats.hit);
    }
#undef KH_REGION
    *count = n;
}

static void kh_discard(kh_edit_t *e)
{
    free(e->runs);
    free(e->bank);
    ks_drop(e->song_before);
    ks_drop(e->song_after);
    *e = (kh_edit_t){0};
}

static void kh_clear(void)
{
    for (int i = 0; i < kh_count; i++) kh_discard(&kh_entries[i]);
    kh_cursor = kh_count = kh_merge = 0;
}

static void kh_reset(void)
{
    kh_clear();
    kh_discard(&kh_edit.edit);
    kh_discard(&kh_take.edit);
    kh_edit.active = kh_take.active = kh_home = kh_visible = kh_memory_full = 0;
}

/* Encode changed spans without touching neighbouring bytes or transport state. */
static size_t kh_encode(const uint8_t *before, const kh_region_t *r, int count, uint8_t *out)
{
    size_t at = 0, base = 0;
    for (int k = 0; k < count; k++) {
        const uint8_t *after = r[k].bytes;
        for (size_t start = 0; start < r[k].size;) {
            if (before[base + start] == after[start]) {
                start++;
                continue;
            }
            size_t end = start + 1;
            while (end < r[k].size && before[base + end] != after[end]) end++;
            size_t offset = base + start, len = end - start;
            if (out) {
                out[at] = (uint8_t)offset; out[at + 1] = (uint8_t)(offset >> 8);
                out[at + 2] = (uint8_t)len; out[at + 3] = (uint8_t)(len >> 8);
                memcpy(out + at + 4, before + offset, len);
                memcpy(out + at + 4 + len, after + start, len);
            }
            at += 4 + 2 * len;
            start = end;
        }
        base += r[k].size;
    }
    return at;
}

static void kh_decode(const kh_edit_t *e, const kh_region_t *r, uint8_t *before, int redo)
{
    int k = 0;
    size_t base = 0;
    for (size_t i = 0; i < e->size;) {
        size_t offset = (size_t)e->runs[i] | (size_t)e->runs[i + 1] << 8;
        size_t len = (size_t)e->runs[i + 2] | (size_t)e->runs[i + 3] << 8;
        i += 4;
        if (before) memcpy(before + offset, e->runs + i, len);
        else {
            while (offset >= base + r[k].size) base += r[k++].size;
            memcpy(r[k].bytes + offset - base, e->runs + i + (redo ? len : 0), len);
        }
        i += 2 * len;
    }
}

static void kh_begin(kh_capture_t *c, int scope, const char *name, int track, unsigned group)
{
    kh_region_t r[32];
    int n;
    size_t pos = 0;
    c->active = 1;
    c->edit = (kh_edit_t){.name = name, .group = (uint16_t)group, .scope = (uint8_t)scope,
        .track = (uint8_t)track, .pattern = (uint8_t)cur_pat(track), .cell = kids_cell};
    kh_regions(&c->edit, r, &n);
    for (int i = 0; i < n; i++) {
        memcpy(c->before + pos, r[i].bytes, r[i].size);
        pos += r[i].size;
    }
}

static int kh_pending_changed(const kh_edit_t *e)
{
    return e->scope == KH_STEP && (e->cell.next_velocity || kids_cell.next_velocity) &&
        memcmp(&e->cell, &kids_cell, sizeof kids_cell) != 0;
}

static void kh_push(kh_edit_t e)
{
    while (kh_count > kh_cursor) kh_discard(&kh_entries[--kh_count]);
    if (kh_count == KH_LIMIT) {
        kh_discard(&kh_entries[0]);
        memmove(kh_entries, kh_entries + 1, (KH_LIMIT - 1) * sizeof kh_entries[0]);
        kh_count--;
    }
    kh_entries[kh_count++] = e;
    kh_cursor = kh_count;
    kh_merge = kh_memory_full = 0;
}

static void kh_end(kh_capture_t *c)
{
    if (!c->active) return;
    c->active = 0;
    kh_edit_t *e = &c->edit;
    kh_region_t r[32];
    int n;
    kh_regions(e, r, &n);
    size_t size = kh_encode(c->before, r, n, NULL);
    e->cell_changed = (uint8_t)kh_pending_changed(e);
    if (!size && !e->bank_changed && !e->cell_changed) return;
    uint32_t now = plat_ms();
    kh_edit_t *previous = kh_cursor ? &kh_entries[kh_cursor - 1] : NULL;
    int merge = e->group && kh_merge && previous && kh_cursor == kh_count &&
        now - kh_touched <= KH_SWEEP_MS && previous->group == e->group &&
        previous->track == e->track && previous->pattern == e->pattern && previous->scope == e->scope;
    if (merge) {
        kh_decode(previous, r, c->before, 0);
        if (previous->cell_changed) e->cell = previous->cell;
        e->cell_changed = (uint8_t)kh_pending_changed(e);
        size = kh_encode(c->before, r, n, NULL);
    }
    if (e->cell_changed) e->cell_after = kids_cell;
    e->runs = size ? malloc(size) : NULL;
    if (size && !e->runs) {
        /* The current edit still exists. Drop the journal to avoid stale deltas. */
        kh_discard(e);
        kh_clear();
        kh_memory_full = 1;
        return;
    }
    e->size = kh_encode(c->before, r, n, e->runs);
    if (merge) {
        kh_discard(&kh_entries[--kh_count]);
        kh_cursor = kh_count;
    }
    if (e->size || e->bank_changed || e->cell_changed) kh_push(*e);
    mark_dirty();
    *e = (kh_edit_t){0};
    kh_cursor = kh_count;
    kh_touched = now;
    kh_merge = (uint8_t)(c == &kh_edit);
    kh_memory_full = 0;
}

static void kh_bank_changed(kids_bank_t *previous)
{
    if (!kh_edit.active) {
        free(previous);
        return;
    }
    kh_edit.edit.bank_changed = 1;
    kh_edit.edit.bank = previous;
}

static const kh_edit_t *kh_next(int redo)
{
    if (redo) return kh_cursor < kh_count ? &kh_entries[kh_cursor] : NULL;
    return kh_cursor ? &kh_entries[kh_cursor - 1] : NULL;
}

static void kh_sync(const kh_edit_t *e, const trk_t *old, const uint8_t old_shape[KD_COUNT],
                    const uint8_t old_fx[2][4], int tempo, int groove, int delay, int metro)
{
    int t = e->track;
    const trk_t *sound = &proj.trk[t];
    if (e->scope == KH_SOUND) engine_set_all(t);
    else for (int p = 0; p < NTP; p++)
        if (p != TP_PLAYER && old->par[p] != sound->par[p]) engine_set(t, p, sound->par[p]);
    if (old->par[TP_PLAYER] != sound->par[TP_PLAYER] || e->bank_changed)
        engine_kids_player(t, sound->par[TP_PLAYER]);
    for (int p = 0; p < KD_COUNT; p++)
        if (old_shape[p] != kids_shaping[t][p]) engine_kids_dsp(t, p, kids_shaping[t][p]);
    for (int slot = 0; slot < 2; slot++)
        kids_send_gain[t][slot] = kids_fx_on[t][slot] ? kids_fx_amount[t][slot] : 0;
    if (tempo != proj.tempo_x10) engine_tempo(proj.tempo_x10);
    if (groove != proj.groove) engine_groove(proj.groove);
    if (metro != kids_metro) engine_kids_metro(kids_metro);
    if (tempo != proj.tempo_x10 || delay != kids_delay_choice ||
        memcmp(old_fx[0], proj.trk[AUX_FX1].par + TP_E1, 4)) kids_update_delay();
    if (memcmp(old_fx[1], proj.trk[AUX_FX2].par + TP_E1, 4)) apply_fx(AUX_FX2);
    if (e->bank_changed) {
        for (int i = 0; i < NKITM; i++) seq.kmute[i] = pats.scene[cur_scene()].kmute[i];
        kids_cell.track = -1;
    }
    kids_sound_error = NULL;
    kids_category_sync();
    kids_remember_chord(sel_trk());
    ui.bar = (uint8_t)clampi(ui.bar, 0, kids_page_count(sel_trk()) - 1);
    kids_update_performance_page();
    mark_dirty();
}

static int kh_replay(int redo)
{
    if (!kh_next(redo)) return 0;
    kh_edit_t *e = &kh_entries[redo ? kh_cursor : kh_cursor - 1];
    if (e->scope == KH_SLOT) return ks_replay(e, redo);
    if (e->scope == KH_LOAD) {
        if (!ks_replay(e, redo)) return 0;
        kh_cursor = (uint8_t)(kh_cursor + (redo ? 1 : -1));
        kh_merge = 0;
        return 1;
    }
    int t = e->track;
    if (t == sel_trk() && (e->scope == KH_SOUND || !strcmp(e->name, "Arp switch")))
        kids_notes_off();
    const trk_t old = proj.trk[t];
    uint8_t shape[KD_COUNT], fx[2][4];
    memcpy(shape, kids_shaping[t], sizeof shape);
    for (int i = 0; i < 2; i++) memcpy(fx[i], proj.trk[AUX_FX1 + i].par + TP_E1, 4);
    int tempo = proj.tempo_x10, groove = proj.groove, delay = kids_delay_choice, metro = kids_metro;
    kh_region_t r[32];
    int n;
    kh_regions(e, r, &n);
    kh_decode(e, r, NULL, redo);
    if (e->cell_changed) kids_cell = redo ? e->cell_after : e->cell;
    if (e->bank_changed) {
        kids_bank_t *bank = kids_other[t];
        kids_other[t] = e->bank;
        e->bank = bank;
    }
    kh_sync(e, &old, shape, fx, tempo, groove, delay, metro);
    kh_cursor = (uint8_t)(kh_cursor + (redo ? 1 : -1));
    kh_merge = 0;
    return 1;
}
