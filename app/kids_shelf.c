/* SPDX-License-Identifier: MPL-2.0 */
enum { KS_SLOTS = 3, KS_PIXELS = KS_SLOTS, KS_STARTUP, KS_RECORDS };

#include "kids_song_codec.c"
#include "kids_shelf_native.c"
enum { KS_CLOSED, KS_OPEN_DIRTY, KS_DELETE };
enum { KS_SAVE_WRITE, KS_DELETE_WRITE, KS_RENAME_WRITE, KS_PIXELS_WRITE, KS_STARTUP_WRITE, KS_UNDO_WRITE, KS_REDO_WRITE };
#define KS_PIXEL_MAGIC 0x58504b4cu /* LKPX */
#define KS_DELETE_MS 5000u
#define KS_EDIT_HOLD_MS 350u
typedef struct {
    ks_blob_t *blob;
    uint32_t revision;
} ks_slot_t;

static ks_slot_t ks_slots[KS_RECORDS];
static uint8_t ks_selected, ks_loaded, ks_modal, ks_target, ks_edit_held, ks_glo_held;
static uint8_t ks_ready, ks_bad, ks_saved_slot, ks_startup_dirty, ks_unopened;
static int ks_replay_left;
static uint32_t ks_edit_since, ks_glo_since, ks_delete_until, ks_message_until;
static const char *ks_message;
static struct {
    ks_blob_t *blob;
    uint8_t active, slot, kind, close_open, dirty;
    uint32_t id, pixel_generation;
} ks_write;
static uint32_t ks_id, ks_startup_retry;
enum { KS_DEFER_NONE, KS_DEFER_SAVE, KS_DEFER_NAME, KS_DEFER_DELETE };
static uint8_t ks_deferred, ks_deferred_name[KPIX_BYTES], ks_deferred_id, ks_deferred_slot;

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

static void ks_set_loaded(int slot)
{
    ks_startup_dirty |= ks_loaded != slot;
    ks_selected = ks_loaded = (uint8_t)slot;
    ks_unopened = 0;
}

/* An empty slot uses its own starter name, or the next free picture if
 * another song took that starter after a rename. */
static uint8_t ks_preview_name(int slot, uint8_t *pixels)
{
    if (ks_slots[slot].blob) return ks_read_name(ks_slots[slot].blob, slot, pixels);
    unsigned used = 0;
    uint8_t names[KS_SLOTS], scratch[KPIX_BYTES];
    for (int i = 0; i < KS_SLOTS; i++) {
        names[i] = ks_read_name(ks_slots[i].blob, i, scratch);
        if (ks_slots[i].blob && names[i] < KPIX_COUNT) used |= 1u << names[i];
    }
    for (int i = 0; i < KS_SLOTS; i++) {
        if (ks_slots[i].blob || (used >> names[i] & 1u)) continue;
        used |= 1u << names[i];
        if (i == slot) {
            kpix_default(names[i], pixels);
            return names[i];
        }
        names[i] = KPIX_UNSET;
    }
    for (int i = 0; i <= slot; i++) {
        if (ks_slots[i].blob || names[i] == KPIX_UNSET) continue;
        unsigned next = 0;
        while (used >> next & 1u) next++;
        names[i] = (uint8_t)next;
        used |= 1u << next;
    }
    kpix_default(names[slot], pixels);
    return names[slot];
}

static ks_blob_t *ks_empty_song(int slot)
{
    uint8_t pixels[KPIX_BYTES];
    uint8_t name = ks_preview_name(slot, pixels);
    return ks_new_song(name, pixels);
}

static void ks_ready_start(int startup)
{
    const ks_blob_t *bank = ks_slots[KS_PIXELS].blob;
    if (bank && bank->len >= 8 && get32(bank->data) == KS_PIXEL_MAGIC &&
        get32(bank->data + 4) == crc32_of(bank->data + 8, bank->len - 8)) {
        if (bank->len == 8 + sizeof kpix_bank)
            memcpy(kpix_bank, bank->data + 8, sizeof kpix_bank);
        else if (bank->len == 8 + KPIX_COUNT * KS_LEGACY_PIXELS) {
            for (int i = 0; i < KPIX_COUNT; i++)
                kpix_from_legacy(kpix_bank[i], bank->data + 8 + i * KS_LEGACY_PIXELS);
        } else ks_bad |= 1u << KS_PIXELS;
    } else if (bank) ks_bad |= 1u << KS_PIXELS;

    const ks_blob_t *boot = ks_slots[KS_STARTUP].blob;
    int slot = boot && boot->len == 1 && boot->data[0] < KS_SLOTS ? boot->data[0] : 0;
    const ks_blob_t *saved = ks_slots[slot].blob;
    ks_decoded_t decoded = {0};
    int rc = saved ? ks_unpack(saved, &decoded, 1) : 0;
    ks_unopened = rc != 0;
    if (saved && !rc) ks_apply(&decoded, 0);
    else if (startup || saved) {
        ks_blob_t *empty = ks_empty_song(slot);
        if (empty && !ks_unpack(empty, &decoded, 0)) ks_apply(&decoded, 0);
        ks_drop(empty);
    }
    ks_selected = ks_loaded = (uint8_t)slot;
    kpix_song_id = ks_unopened ? ks_read_name(NULL, slot, kpix_song) : ks_preview_name(slot, kpix_song);
    if (ks_unopened) ks_note(rc == -3 ? "Sample bank needed" : "Song unreadable", slot);
}

static void ks_init(int startup)
{
    ks_drop(ks_write.blob);
    memset(&ks_write, 0, sizeof ks_write);
    ks_selected = ks_target = ks_modal = ks_bad = ks_edit_held = ks_glo_held = ks_deferred = 0;
    ks_loaded = ks_startup_dirty = ks_unopened = 0;
    ks_message = NULL;
    ks_startup_retry = 0;
    ks_replay_left = 0;
    kpix_init();
    kpix_song_id = ks_read_name(NULL, 0, kpix_song);
#ifdef PLAT_WEB
    (void)startup;
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
    ks_ready_start(startup);
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
    ks_blob_t *target = fresh ? ks_empty_song(slot) : ks_keep(ks_slots[slot].blob);
    ks_decoded_t decoded = {0};
    int rc = target ? ks_unpack(target, &decoded, 1) : -2;
    if (rc) {
        ks_drop(before);
        ks_drop(target);
        ks_note(rc == -3 ? "Sample bank needed" : rc == -2 ? "Memory full" : "Song unreadable", slot);
        return 0;
    }
    int dirty = 0;
    kh_push((kh_edit_t){.scope = KH_LOAD, .track = (uint8_t)sel_trk(), .name = "Open song",
        .song_before = before, .song_after = target, .was_dirty = ui.dirty, .after_dirty = (uint8_t)dirty,
        .before_slot = ks_loaded, .after_slot = (uint8_t)slot});
    ks_apply(&decoded, dirty);
    ks_set_loaded(slot);
    kpix_song_id = ks_read_name(target, slot, kpix_song);
    ks_note(fresh ? "Ready to save" : "Opened", slot);
    return 1;
}

static int ks_background(int kind) { return kind == KS_PIXELS_WRITE || kind == KS_STARTUP_WRITE; }

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
        if (kind == KS_STARTUP_WRITE && status != -2) {
            ks_startup_dirty = 1;
            ks_startup_retry = plat_ms() + 1000u;
        }
        ks_note(status == -2 ? "Storage changed. Reload" : "Write failed. Kept", ks_background(kind) ? -1 : slot);
        return 0;
    }
    ks_blob_t *before = ks_slots[slot].blob;
    ks_slots[slot].blob = next;
    ks_slots[slot].revision++;
    if (kind == KS_STARTUP_WRITE) {
        ks_startup_dirty = ks_loaded != next->data[0];
    } else if (kind == KS_PIXELS_WRITE) {
        kpix_dirty = kpix_generation != ks_write.pixel_generation;
    } else if (kind == KS_UNDO_WRITE || kind == KS_REDO_WRITE) {
        int redo = kind == KS_REDO_WRITE;
        const kh_edit_t *e = kh_next(redo);
        if (e && slot == ks_loaded) {
            if (!strcmp(e->name, "Rename song")) kpix_song_id = ks_read_name(next, slot, kpix_song);
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
        if (kind == KS_RENAME_WRITE) kpix_song_id = ks_read_name(next, slot, kpix_song);
        if (kind == KS_DELETE_WRITE && slot == ks_loaded) ks_unopened = 0;
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
    if (!ks_background(kind)) ks_modal = KS_CLOSED;
    ks_write.pixel_generation = kpix_generation;
    ks_write.active = 1;
    ks_write.slot = (uint8_t)slot;
    ks_write.kind = (uint8_t)kind;
    ks_write.close_open = (uint8_t)follow;
    ks_write.dirty = ui.dirty;
    ks_write.id = ++ks_id;
    ks_write.blob = blob;
#ifdef PLAT_WEB
    if (!ks_background(kind)) ks_note("Writing", slot);
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
        ks_set_loaded(redo ? e->after_slot : e->before_slot);
        kpix_song_id = ks_read_name(redo ? e->song_after : e->song_before, ks_loaded, kpix_song);
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
    ks_ready_start(1);
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
    if (ks_unopened) { ks_note("Open saved song first", ks_loaded); return; }
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

static void ks_store_startup(void)
{
    if (!ks_startup_dirty || ks_write.active || !ks_ready || (ks_bad >> KS_STARTUP & 1u) ||
        (ks_startup_retry && (int32_t)(plat_ms() - ks_startup_retry) < 0)) return;
    ks_blob_t *blob = ks_blob(1);
    if (!blob) return;
    ks_startup_dirty = 0;
    ks_startup_retry = 0;
    blob->data[0] = ks_loaded;
    ks_start_write(KS_STARTUP, blob, KS_STARTUP_WRITE, 0);
}

static int ks_name_owner(uint8_t name)
{
    for (int slot = 0; slot < KS_SLOTS; slot++) {
        if (slot == ks_loaded) continue;
        uint8_t used[KPIX_BYTES];
        if (ks_preview_name(slot, used) == name) return slot;
    }
    return -1;
}

static void ks_assign_name(uint8_t name, const uint8_t *pixels)
{
    if (ks_unopened) { ks_note("Open saved song first", ks_loaded); return; }
    if (!ks_slot_available(ks_loaded)) return;
    int owner = ks_name_owner(name);
    if (owner >= 0) {
        ks_note("Name already in use", owner);
        return;
    }
    if (kpix_song_id == name && !memcmp(kpix_song, pixels, KPIX_BYTES)) { ks_note("Named", ks_loaded); return; }
    const ks_blob_t *stored = ks_slots[ks_loaded].blob;
    if (!stored) {
        kh_begin(&kh_edit, KH_NAME, "Rename song", sel_trk(), 0);
        memcpy(kpix_song, pixels, KPIX_BYTES);
        kpix_song_id = name;
        kh_end(&kh_edit);
        ks_note("Name ready. SAVE", ks_loaded);
        return;
    }
    ks_blob_t *renamed = ks_rename(stored, name, pixels);
    if (!renamed) { ks_note("Could not name song", ks_loaded); return; }
    ks_start_write(ks_loaded, renamed, KS_RENAME_WRITE, 0);
}

static void ks_open_preview(void)
{
    if (!ks_slot_available(ks_selected)) return;
    if (seq.rec || seq.rec_wait) { ks_note("End recording first", -1); return; }
    if (ui.dirty && (ks_slots[ks_selected].blob ||
        ks_selected != ks_loaded)) {
        ks_target = ks_selected;
        ks_modal = KS_OPEN_DIRTY;
    } else ks_open(ks_selected);
}

static int ks_input(uint32_t down, const int32_t ev[NE])
{
    const uint32_t home = 1u << B_HOME, save = 1u << B_SAVE, edit = 1u << B_EDIT, glo = 1u << B_GLO;
    int on_shelf = kids_mode == KIDS_HOME && kids_panel == KP_GLOBAL;
    if (ks_deferred && !ks_write.active) {
        int action = ks_deferred;
        ks_deferred = KS_DEFER_NONE;
        if (action == KS_DEFER_SAVE) ks_save_to(0);
        else if (action == KS_DEFER_NAME) ks_assign_name(ks_deferred_id, ks_deferred_name);
        else if (ks_slots[ks_deferred_slot].blob) ks_start_write(ks_deferred_slot, NULL, KS_DELETE_WRITE, 0);
        return 1;
    }
    if (ks_write.active && !ks_background(ks_write.kind)) {
        if (kpix_open && (down & home)) {
            kpix_open = 0;
            ks_edit_held = 0;
        }
        if (kh_home && (ui.btn & home) && ev[EN_K1])
            ks_replay_left = clampi(ks_replay_left + clampi(ev[EN_K1], -KH_LIMIT, KH_LIMIT), -KH_LIMIT, KH_LIMIT);
        return 1;
    }
    if (!(ui.btn & glo) || (down & (home | (1u << B_SEQ)))) ks_glo_held = 0;
    if (ks_modal == KS_DELETE && (int32_t)(ks_delete_until - plat_ms()) <= 0) {
        ks_modal = KS_CLOSED;
        return 1; /* consume activity at the timeout boundary */
    }
    if (ks_modal) {
        if (down & home) ks_modal = KS_CLOSED;
        else if (down & edit) {
            if (ks_modal == KS_DELETE) {
                ks_modal = KS_CLOSED;
                if (ks_slots[ks_target].blob) {
                    if (!ks_write.active) ks_start_write(ks_target, NULL, KS_DELETE_WRITE, 0);
                    else {
                        ks_deferred_slot = ks_target;
                        ks_deferred = KS_DEFER_DELETE;
                    }
                }
            } else {
                ks_modal = KS_CLOSED;
                ks_open(ks_target);
                ks_store_startup();
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
                ks_deferred_id = kpix_slot;
                ks_deferred = KS_DEFER_NAME;
            } else ks_assign_name(kpix_slot, kpix_bank[kpix_slot]);
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
    if (down & glo) {
        ks_glo_since = plat_ms();
        ks_glo_held = 1;
    }
    if (on_shelf && !(down & (glo | (1u << B_SEQ)))) {
        ks_selected = (uint8_t)clampi(ks_selected + clampi(ev[EN_K4], -KS_SLOTS, KS_SLOTS), 0, KS_SLOTS - 1);
        if (ks_glo_held == 1 && plat_ms() - ks_glo_since >= KS_DELETE_MS) {
            ks_glo_held = 2;
            if (ks_slot_available(ks_selected)) {
                ks_target = ks_selected;
                ks_modal = KS_DELETE;
                ks_delete_until = plat_ms() + KS_DELETE_MS;
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
                ks_store_startup();
            }
            return 1;
        }
    } else ks_edit_held = 0;
    ks_store_startup();
    ks_flush_pixels(0);
    return 0;
}
