/* SPDX-License-Identifier: MPL-2.0 */
enum { KS_SLOTS = 3, KS_PIXELS = KS_SLOTS, KS_RECORDS, KS_NONE = KS_RECORDS };

#include "kids_song_codec.c"
#include "kids_shelf_native.c"
enum { KS_CLOSED, KS_OPEN_DIRTY, KS_DELETE };
enum { KS_SAVE_WRITE, KS_DELETE_WRITE, KS_RENAME_WRITE, KS_PIXELS_WRITE, KS_UNDO_WRITE, KS_REDO_WRITE };
#define KS_PIXEL_MAGIC 0x58504b4cu /* LKPX */
#define KS_DELETE_MS 5000u
#define KS_EDIT_HOLD_MS 350u
typedef struct {
    ks_blob_t *blob;
    uint32_t revision;
} ks_slot_t;

static ks_slot_t ks_slots[KS_RECORDS];
static uint8_t ks_selected, ks_loaded, ks_modal, ks_target, ks_edit_held, ks_delete_latched;
static uint8_t ks_ready, ks_bad, ks_saved_slot;
static int ks_replay_left;
static uint32_t ks_edit_since, ks_delete_until, ks_message_until;
static const char *ks_message;
static struct {
    ks_blob_t *blob;
    uint8_t active, slot, kind, close_open, dirty;
    uint32_t id, pixel_generation;
} ks_write;
static uint32_t ks_id;
enum { KS_DEFER_NONE, KS_DEFER_SAVE, KS_DEFER_NAME };
static uint8_t ks_deferred, ks_deferred_name[KPIX_BYTES];

#ifdef PLAT_WEB
static uint8_t ks_incoming[KS_BLOB_MAX];
static uint8_t ks_seed_pending;
#endif

static void ks_note(const char *message, int slot)
{
    ks_message = message;
    ks_saved_slot = (uint8_t)(slot + 1);
    ks_message_until = plat_ms() + 1800u;
}

static int ks_note_active(void)
{
    return ks_message && (int32_t)(ks_message_until - plat_ms()) > 0;
}

static void ks_ready_start(void)
{
    if (ks_ready && !(ks_bad & 7u) && !ks_slots[0].blob && !ks_slots[1].blob && !ks_slots[2].blob)
        ks_loaded = 0;
    const ks_blob_t *bank = ks_slots[KS_PIXELS].blob;
    if (!bank) return;
    if (bank->len == 8 + sizeof kpix_bank && get32(bank->data) == KS_PIXEL_MAGIC &&
        get32(bank->data + 4) == crc32_of(bank->data + 8, sizeof kpix_bank))
        memcpy(kpix_bank, bank->data + 8, sizeof kpix_bank);
    else ks_bad |= 1u << KS_PIXELS;
}

static void ks_apply_name(const ks_blob_t *blob)
{
    const uint8_t *name = ks_name(blob);
    if (name) memcpy(kpix_song, name, KPIX_BYTES);
    else memset(kpix_song, 0, KPIX_BYTES);
}

static void ks_init(void)
{
    ks_drop(ks_write.blob);
    memset(&ks_write, 0, sizeof ks_write);
    ks_selected = ks_target = ks_modal = ks_bad = ks_edit_held = ks_delete_latched = ks_deferred = 0;
    ks_loaded = KS_NONE;
    ks_message = NULL;
    ks_replay_left = 0;
    kpix_init();
#ifdef PLAT_WEB
    for (int slot = 0; slot < KS_RECORDS; slot++) {
        ks_drop(ks_slots[slot].blob);
        ks_slots[slot] = (ks_slot_t){0};
    }
    ks_ready = 0;
    ks_seed_pending = 1;
#else
    ks_ready = 1;
    for (int slot = 0; slot < KS_RECORDS; slot++) {
        char path[1024];
        int backend = ks_file_path(path, sizeof path, slot, ".ks");
        if (!backend) continue;
        ks_drop(ks_slots[slot].blob);
        ks_slots[slot] = (ks_slot_t){0};
        if (backend < 0 || ks_file_read(path, &ks_slots[slot].revision, &ks_slots[slot].blob))
            ks_bad |= 1u << slot;
    }
    ks_ready_start();
#endif
}

static int ks_open(int slot)
{
    if (slot == ks_loaded && !ks_slots[slot].blob) {
        ks_note("Ready to save", slot);
        return 1;
    }
    ks_blob_t *before = ks_pack();
    if (!before) { ks_note("Memory full", -1); return 0; }
    int fresh = !ks_slots[slot].blob;
    int attach = fresh && ks_loaded == KS_NONE;
    ks_blob_t *target = fresh ? (attach ? ks_keep(before) : ks_new_song()) : ks_keep(ks_slots[slot].blob);
    ks_decoded_t decoded = {0};
    int rc = target ? (attach ? 0 : ks_unpack(target, &decoded, 1)) : -2;
    if (rc) {
        ks_drop(before);
        ks_drop(target);
        ks_note(rc == -3 ? "Sample bank needed" : rc == -2 ? "Memory full" : "Song unreadable", slot);
        return 0;
    }
    int dirty = attach && ui.dirty;
    kh_push((kh_edit_t){.scope = KH_LOAD, .track = (uint8_t)sel_trk(), .name = "Open song",
        .song_before = before, .song_after = target, .was_dirty = ui.dirty, .after_dirty = (uint8_t)dirty,
        .before_slot = ks_loaded, .after_slot = (uint8_t)slot});
    if (!attach) ks_apply(&decoded, dirty);
    ks_apply_name(target);
    ks_loaded = ks_selected = (uint8_t)slot;
    ks_note(fresh ? "Ready to save" : "Opened", slot);
    return 1;
}

static int ks_finish_write(int status)
{
    int slot = ks_write.slot, kind = ks_write.kind, open = ks_write.close_open;
    ks_blob_t *next = ks_write.blob;
    ks_write.blob = NULL;
    ks_write.active = 0;
    if (status) {
        ks_drop(next);
        ks_replay_left = 0;
        if (status == -2) ks_bad |= 1u << slot;
        if (kind == KS_PIXELS_WRITE) kpix_changed = plat_ms() + 1000u;
        ks_note(status == -2 ? "Storage changed. Reload" : "Write failed. Kept", slot == KS_PIXELS ? -1 : slot);
        return 0;
    }
    ks_blob_t *before = ks_slots[slot].blob;
    ks_slots[slot].blob = next;
    ks_slots[slot].revision++;
    if (kind == KS_PIXELS_WRITE) {
        kpix_dirty = kpix_generation != ks_write.pixel_generation;
    } else if (kind == KS_UNDO_WRITE || kind == KS_REDO_WRITE) {
        int redo = kind == KS_REDO_WRITE;
        const kh_edit_t *e = kh_next(redo);
        if (e && slot == ks_loaded) {
            if (!strcmp(e->name, "Rename song")) ks_apply_name(next);
            else if (strcmp(e->name, "Delete song")) ui.dirty = redo ? e->after_dirty : e->was_dirty;
        }
        kh_cursor = (uint8_t)(kh_cursor + (redo ? 1 : -1));
        kh_merge = 0;
        ks_note(redo ? "Redone" : "Undone", slot);
    } else {
        kh_push((kh_edit_t){.scope = KH_SLOT, .track = (uint8_t)sel_trk(), .slot = (uint8_t)slot,
            .name = kind == KS_RENAME_WRITE ? "Rename song" : kind == KS_DELETE_WRITE ? "Delete song" : before ? "Replace song" : "Save song",
            .song_before = ks_keep(before), .song_after = ks_keep(next),
            .was_dirty = ks_write.dirty, .after_dirty = 0});
        if (kind == KS_SAVE_WRITE) ui.dirty = 0;
        if (kind == KS_RENAME_WRITE) ks_apply_name(next);
        ks_note(kind == KS_RENAME_WRITE ? "Named" : kind == KS_DELETE_WRITE ? "Deleted" : "Saved", slot);
        if (open) ks_open(open - 1);
    }
    ks_drop(before);
    return 1;
}

/* Takes ownership of blob even on failure. A browser ack is the commit point. */
static int ks_start_write(int slot, ks_blob_t *blob, int kind, int follow)
{
    if (!ks_ready || ks_write.active || (ks_bad >> slot & 1u) || ks_slots[slot].revision == UINT32_MAX) {
        ks_drop(blob);
        ks_note("Slot unavailable", slot);
        return 0;
    }
    if (kind != KS_PIXELS_WRITE) ks_modal = KS_CLOSED;
    ks_write.pixel_generation = kpix_generation;
    ks_write.active = 1;
    ks_write.slot = (uint8_t)slot;
    ks_write.kind = (uint8_t)kind;
    ks_write.close_open = (uint8_t)follow;
    ks_write.dirty = ui.dirty;
    ks_write.id = ++ks_id;
    ks_write.blob = blob;
#ifdef PLAT_WEB
    if (kind != KS_PIXELS_WRITE) ks_note("Writing", slot);
    return 0;
#else
    return ks_finish_write(ks_file_write(slot, ks_slots[slot].revision, blob));
#endif
}

static int ks_replay(const kh_edit_t *e, int redo)
{
    if (e->scope == KH_LOAD) {
        ks_decoded_t decoded = {0};
        if (ks_unpack(redo ? e->song_after : e->song_before, &decoded, 0)) {
            ks_note("Song history unreadable", -1);
            return 0;
        }
        ks_apply(&decoded, redo ? e->after_dirty : e->was_dirty);
        ks_loaded = redo ? e->after_slot : e->before_slot;
        ks_apply_name(redo ? e->song_after : e->song_before);
        return 1;
    }
    int slot = e->slot;
    if (!ks_same(ks_slots[slot].blob, redo ? e->song_before : e->song_after)) {
        ks_note("Slot changed. Reload", slot);
        return 0;
    }
    return ks_start_write(slot, ks_keep(redo ? e->song_after : e->song_before),
        redo ? KS_REDO_WRITE : KS_UNDO_WRITE, 0);
}

static void ks_replay_turn(int turn)
{
    int remaining = clampi(turn, -KH_LIMIT, KH_LIMIT), redo = remaining > 0;
    while (remaining) {
        remaining += redo ? -1 : 1;
        if (kh_replay(redo)) continue;
        if (ks_write.active) ks_replay_left = remaining;
        break;
    }
}

#ifdef PLAT_WEB
/* The simulator suppresses device attributes inside its unity include. */
#pragma push_macro("__attribute__")
#undef __attribute__
#define KS_EXPORT __attribute__((used, visibility("default")))
KS_EXPORT uint32_t web_kids_seed_pending(void) { return ks_seed_pending; }
KS_EXPORT uint8_t *web_kids_incoming(void) { return ks_incoming; }
KS_EXPORT uint32_t web_kids_max(void) { return KS_BLOB_MAX; }
KS_EXPORT void web_kids_seed(uint32_t slot, uint32_t revision, uint32_t len, uint32_t invalid)
{
    if (slot >= KS_RECORDS || !ks_seed_pending) return;
    ks_slot_t *s = &ks_slots[slot];
    ks_drop(s->blob);
    *s = (ks_slot_t){.revision = revision};
    if (len) {
        s->blob = ks_blob(len);
        if (s->blob) memcpy(s->blob->data, ks_incoming, len);
        else invalid = 1;
    }
    if (invalid) ks_bad |= 1u << slot;
}
KS_EXPORT void web_kids_ready(uint32_t ok)
{
    ks_ready = (uint8_t)(ok != 0);
    ks_seed_pending = 0;
    ks_ready_start();
}
KS_EXPORT uint32_t web_kids_write_id(void) { return ks_write.active ? ks_write.id : 0; }
KS_EXPORT uint32_t web_kids_write_slot(void) { return ks_write.slot; }
KS_EXPORT uint32_t web_kids_write_rev(void) { return ks_slots[ks_write.slot].revision; }
KS_EXPORT uint32_t web_kids_write_len(void) { return ks_write.blob ? ks_write.blob->len : 0; }
KS_EXPORT const uint8_t *web_kids_write_data(void) { return ks_write.blob ? ks_write.blob->data : NULL; }
KS_EXPORT void web_kids_complete(uint32_t id, int status)
{
    if (!ks_write.active || id != ks_write.id) return;
    if (ks_finish_write(status) && ks_replay_left) {
        int turn = ks_replay_left;
        ks_replay_left = 0;
        ks_replay_turn(turn);
    }
}
#undef KS_EXPORT
#pragma pop_macro("__attribute__")
#endif

static int ks_slot_available(int slot)
{
    if (ks_ready && !(ks_bad >> slot & 1u)) return 1;
    ks_note("Slot unavailable", slot);
    return 0;
}

static void ks_save_to(int follow)
{
    if (ks_loaded >= KS_SLOTS) { ks_note("Open a slot in GLO", -1); return; }
    if (!ks_slot_available(ks_loaded)) return;
    ks_blob_t *song = ks_pack();
    if (!song) { ks_note("Memory full", -1); return; }
    ks_start_write(ks_loaded, song, KS_SAVE_WRITE, follow);
}

static void ks_flush_pixels(int now)
{
    if (!kpix_dirty || ks_write.active || !ks_ready || (ks_bad >> KS_PIXELS & 1u) ||
        (!now && (int32_t)(plat_ms() - kpix_changed) < 180)) return;
    ks_blob_t *blob = ks_blob(8 + sizeof kpix_bank);
    if (!blob) { kpix_changed = plat_ms() + 1000u; ks_note("Memory full", -1); return; }
    put32(blob->data, KS_PIXEL_MAGIC);
    put32(blob->data + 4, crc32_of((const uint8_t *)kpix_bank, sizeof kpix_bank));
    memcpy(blob->data + 8, kpix_bank, sizeof kpix_bank);
    ks_start_write(KS_PIXELS, blob, KS_PIXELS_WRITE, 0);
}

static void ks_assign_name(const uint8_t *pixels)
{
    if (ks_loaded >= KS_SLOTS) { ks_note("Open a song slot first", -1); return; }
    if (!ks_slot_available(ks_loaded)) return;
    if (!memcmp(kpix_song, pixels, KPIX_BYTES)) { ks_note("Named", ks_loaded); return; }
    const ks_blob_t *stored = ks_slots[ks_loaded].blob;
    if (!stored) {
        kh_begin(&kh_edit, KH_NAME, "Rename song", sel_trk(), 0);
        memcpy(kpix_song, pixels, KPIX_BYTES);
        kh_end(&kh_edit);
        ks_note("Name ready. SAVE", ks_loaded);
        return;
    }
    ks_blob_t *renamed = ks_rename(stored, pixels);
    if (!renamed) { ks_note("Could not name song", ks_loaded); return; }
    ks_start_write(ks_loaded, renamed, KS_RENAME_WRITE, 0);
}

static void ks_open_preview(void)
{
    if (!ks_slot_available(ks_selected)) return;
    if (seq.rec || seq.rec_wait) { ks_note("End recording first", -1); return; }
    if (ui.dirty && (ks_slots[ks_selected].blob ||
        (ks_loaded < KS_SLOTS && ks_selected != ks_loaded))) {
        ks_target = ks_selected;
        ks_modal = KS_OPEN_DIRTY;
    } else ks_open(ks_selected);
}

static int ks_input(uint32_t down, const int32_t ev[NE])
{
    const uint32_t home = 1u << B_HOME, save = 1u << B_SAVE, edit = 1u << B_EDIT;
    int on_shelf = kids_mode == KIDS_HOME && kids_panel == KP_GLOBAL;
    if (ks_deferred && !ks_write.active) {
        int action = ks_deferred;
        ks_deferred = KS_DEFER_NONE;
        if (action == KS_DEFER_SAVE) ks_save_to(0);
        else ks_assign_name(ks_deferred_name);
        return 1;
    }
    if (ks_write.active && ks_write.kind != KS_PIXELS_WRITE) {
        if (kpix_open && (down & home)) {
            kpix_open = 0;
            ks_edit_held = 0;
        }
        if (kh_home && (ui.btn & home) && ev[EN_K1])
            ks_replay_left = clampi(ks_replay_left + clampi(ev[EN_K1], -KH_LIMIT, KH_LIMIT), -KH_LIMIT, KH_LIMIT);
        return 1;
    }
    if (!ev[EN_K4]) ks_delete_latched = 0;
    int delete_turn = ev[EN_K4] && !ks_delete_latched;
    if (ev[EN_K4]) ks_delete_latched = 1;
    if (ks_modal == KS_DELETE && (int32_t)(ks_delete_until - plat_ms()) <= 0) {
        ks_modal = KS_CLOSED;
        return 1; /* consume activity at the timeout boundary */
    }
    if (ks_modal) {
        if (down & home) ks_modal = KS_CLOSED;
        else if ((down & edit) && !ks_write.active) {
            if (ks_modal == KS_DELETE) ks_start_write(ks_target, NULL, KS_DELETE_WRITE, 0);
            else {
                ks_modal = KS_CLOSED;
                ks_open(ks_target);
            }
        } else if (ks_modal == KS_OPEN_DIRTY && (down & save) && !ks_write.active &&
                   ks_loaded < KS_SLOTS && ks_loaded != ks_target) {
            ks_save_to(ks_target + 1);
        }
        return 1;
    }
    if (kpix_open) {
        if (down & home) {
            kpix_open = 0;
            ks_edit_held = 0;
            ks_flush_pixels(1);
            return 1;
        }
        if (ks_edit_held && !(ui.btn & edit)) ks_edit_held = 0;
        if ((down & edit) && !ks_edit_held) {
            if (ks_write.active) {
                memcpy(ks_deferred_name, kpix_bank[kpix_slot], KPIX_BYTES);
                ks_deferred = KS_DEFER_NAME;
            } else ks_assign_name(kpix_bank[kpix_slot]);
        }
        if (down & (1u << B_SEL)) { kpix_reset_slot(kpix_slot); kpix_touch(); }
        int previous = kpix_slot;
        kpix_slot = (uint8_t)clampi(kpix_slot + clampi(ev[EN_K1], -KPIX_COUNT, KPIX_COUNT), 0, KPIX_COUNT - 1);
        kpix_x = (uint8_t)clampi(kpix_x + clampi(ev[EN_K2], -KPIX_SIDE, KPIX_SIDE), 0, KPIX_SIDE - 1);
        kpix_y = (uint8_t)clampi(kpix_y + clampi(ev[EN_K3], -KPIX_SIDE, KPIX_SIDE), 0, KPIX_SIDE - 1);
        if (ev[EN_K4] || (down & (1u << B_PLAY))) {
            kpix_color = (uint8_t)clampi(kpix_color + clampi(ev[EN_K4], -KPIX_COLORS, KPIX_COLORS), 0, KPIX_COLORS - 1);
            if (kpix_get(kpix_bank[kpix_slot], kpix_x, kpix_y) != kpix_color) {
                kpix_put(kpix_bank[kpix_slot], kpix_x, kpix_y, kpix_color);
                kpix_touch();
            }
        }
        ks_flush_pixels(kpix_slot != previous);
        return 1;
    }
    if (ui.btn & home || kh_home) { ks_edit_held = 0; return 0; }
    if (down & save) {
        if (ks_write.active) ks_deferred = KS_DEFER_SAVE;
        else ks_save_to(0);
        return 1;
    }
    if (on_shelf && !(down & ((1u << B_GLO) | (1u << B_SEQ)))) {
        ks_selected = (uint8_t)clampi(ks_selected + clampi(ev[EN_K3], -KS_SLOTS, KS_SLOTS), 0, KS_SLOTS - 1);
        if (delete_turn && !ks_write.active) {
            if (ks_slot_available(ks_selected)) {
                if (!ks_slots[ks_selected].blob) ks_note("Empty slot", ks_selected);
                else {
                    ks_target = ks_selected;
                    ks_modal = KS_DELETE;
                    ks_delete_until = plat_ms() + KS_DELETE_MS;
                }
            }
            return 1;
        }
        if (down & edit) { ks_edit_since = plat_ms(); ks_edit_held = 1; }
        if (ks_edit_held) {
            if (ui.btn & edit) {
                if (plat_ms() - ks_edit_since >= KS_EDIT_HOLD_MS) {
                    kids_notes_off();
                    kpix_open = 1;
                }
            } else {
                ks_edit_held = 0;
                ks_open_preview();
            }
            return 1;
        }
    } else ks_edit_held = 0;
    ks_flush_pixels(0);
    return 0;
}
