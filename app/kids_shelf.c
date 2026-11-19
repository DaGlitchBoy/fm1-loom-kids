/* SPDX-License-Identifier: MPL-2.0 */
#include "kids_song_codec.c"
#include "kids_shelf_native.c"

#define KS_SLOTS 3
enum { KS_CLOSED, KS_OPEN_DIRTY, KS_PIN_REPLACE, KS_PIN_DELETE, KS_REPLACE, KS_DELETE };
enum { KS_SAVE_WRITE, KS_DELETE_WRITE, KS_UNDO_WRITE, KS_REDO_WRITE };
typedef struct {
    ks_blob_t *blob;
    uint32_t revision;
} ks_slot_t;

static ks_slot_t ks_slots[KS_SLOTS];
static uint8_t ks_selected, ks_modal, ks_target, ks_adult, ks_glo_count;
static uint8_t ks_ready, ks_bad, ks_saved_slot, ks_pin_digits;
static int ks_replay_left;
static uint32_t ks_glo_first, ks_pin_entry, ks_pin, ks_message_until;
static const char *ks_message;
static struct {
    ks_blob_t *blob;
    uint8_t active, slot, kind, close_open, dirty;
    uint32_t id;
} ks_write;
static uint32_t ks_id;

#ifdef PLAT_WEB
static uint8_t ks_incoming[KS_BLOB_MAX];
static uint8_t ks_seed_pending;
#endif

static uint32_t ks_pin_token(uint32_t pin)
{
    return (pin ^ 0x4c4f4f4du) * 2654435761u;
}

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

static void ks_init(void)
{
    ks_drop(ks_write.blob);
    memset(&ks_write, 0, sizeof ks_write);
    ks_selected = ks_target = ks_adult = ks_glo_count = ks_modal = ks_bad = ks_pin_digits = 0;
    ks_message = NULL;
    ks_replay_left = 0;
    ks_pin = 0;
#ifdef PLAT_WEB
    for (int slot = 0; slot < KS_SLOTS; slot++) {
        ks_drop(ks_slots[slot].blob);
        ks_slots[slot] = (ks_slot_t){0};
    }
    ks_ready = 0;
    ks_seed_pending = 1;
#else
    ks_ready = 1;
    for (int slot = 0; slot < KS_SLOTS; slot++) {
        char path[1024];
        int backend = ks_file_path(path, sizeof path, slot, ".ks");
        if (!backend) continue;  /* the host's ephemeral shelf survives a simulated reboot */
        ks_drop(ks_slots[slot].blob);
        ks_slots[slot] = (ks_slot_t){0};
        if (backend < 0 || ks_file_read(path, &ks_slots[slot].revision, &ks_slots[slot].blob))
            ks_bad |= 1u << slot;
    }
    const char *pin = getenv("LOOM_KIDS_PIN");
    if (pin && strlen(pin) == 4 && strspn(pin, "0123456789") == 4)
        ks_pin = ks_pin_token((uint32_t)atoi(pin));
#endif
}

static int ks_open(int slot)
{
    ks_blob_t *target = ks_slots[slot].blob;
    ks_decoded_t decoded = {0};
    int rc = ks_unpack(target, &decoded, 1);
    if (rc) {
        ks_note(rc == -3 ? "Sample bank needed" : rc == -2 ? "Memory full" : "Song unreadable", slot);
        return 0;
    }
    ks_blob_t *before = ks_pack();
    if (!before) { ks_unload(&decoded); ks_note("Memory full", -1); return 0; }
    kh_push((kh_edit_t){.scope = KH_LOAD, .track = (uint8_t)sel_trk(), .name = "Open song",
        .song_before = before, .song_after = ks_keep(target), .was_dirty = ui.dirty});
    ks_apply(&decoded, 0);
    ks_selected = (uint8_t)slot;
    ks_note("Opened", slot);
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
        ks_note(status == -2 ? "Slot changed. Reload" : "Write failed. Kept", slot);
        return 0;
    }
    ks_blob_t *before = ks_slots[slot].blob;
    ks_slots[slot].blob = next;
    ks_slots[slot].revision++;
    if (kind == KS_UNDO_WRITE || kind == KS_REDO_WRITE) {
        int redo = kind == KS_REDO_WRITE;
        const kh_edit_t *e = kh_next(redo);
        if (e && strcmp(e->name, "Delete song")) ui.dirty = redo ? e->after_dirty : e->was_dirty;
        kh_cursor = (uint8_t)(kh_cursor + (redo ? 1 : -1));
        kh_merge = 0;
        ks_note(redo ? "Redone" : "Undone", slot);
    } else {
        kh_push((kh_edit_t){.scope = KH_SLOT, .track = (uint8_t)sel_trk(), .slot = (uint8_t)slot,
            .name = kind == KS_DELETE_WRITE ? "Delete song" : before ? "Replace song" : "Save song",
            .song_before = ks_keep(before), .song_after = ks_keep(next),
            .was_dirty = ks_write.dirty, .after_dirty = 0});
        if (kind == KS_SAVE_WRITE) ui.dirty = 0;
        ks_selected = (uint8_t)slot;
        ks_note(kind == KS_DELETE_WRITE ? "Deleted" : "Saved", slot);
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
    ks_modal = KS_CLOSED;
    ks_write.active = 1;
    ks_write.slot = (uint8_t)slot;
    ks_write.kind = (uint8_t)kind;
    ks_write.close_open = (uint8_t)follow;
    ks_write.dirty = ui.dirty;
    ks_write.id = ++ks_id;
    ks_write.blob = blob;
#ifdef PLAT_WEB
    ks_note("Writing", slot);
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
    if (slot >= KS_SLOTS || !ks_seed_pending) return;
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
}
KS_EXPORT void web_kids_pin(uint32_t token) { ks_pin = token; }
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

static int ks_empty_slot(int start)
{
    for (int i = 0; i < KS_SLOTS; i++) {
        int slot = (start + i) % KS_SLOTS;
        if (!(ks_bad >> slot & 1u) && !ks_slots[slot].blob) return slot;
    }
    return -1;
}

static void ks_save_to(int slot, int open)
{
    /* Audio and UI advance serially in both emulators. Save the pattern as it
     * stands at this instant, including notes from an ongoing take. */
    ks_blob_t *song = ks_pack();
    if (!song) { ks_note("Memory full", -1); return; }
    ks_start_write(slot, song, KS_SAVE_WRITE, open);
}

static void ks_save(void)
{
    int slot = ks_selected;
    if (!ks_slot_available(slot)) return;
    int empty = ks_adult ? slot : ks_empty_slot(slot);
    if (empty >= 0) {
        ks_save_to(empty, 0);
        return;
    }
    if (!ks_pin) { ks_note("Set PIN or use GLO x5", -1); return; }
    ks_target = (uint8_t)slot;
    ks_pin_digits = 0;
    ks_pin_entry = 0;
    ks_modal = KS_PIN_REPLACE;
}

static void ks_delete(void)
{
    int slot = ks_selected;
    if (!ks_slot_available(slot)) return;
    if (!ks_slots[slot].blob) { ks_note("Empty slot", slot); return; }
    if (!ks_adult && !ks_pin) { ks_note("Set PIN or use GLO x5", -1); return; }
    ks_target = (uint8_t)slot;
    ks_pin_digits = 0;
    ks_pin_entry = 0;
    ks_modal = ks_adult ? KS_DELETE : KS_PIN_DELETE;
}

static void ks_glo(void)
{
    uint32_t now = plat_ms();
    if (!ks_glo_count || now - ks_glo_first > 3000u) {
        ks_glo_first = now;
        ks_glo_count = 0;
    }
    if (++ks_glo_count == 5) {
        ks_glo_count = 0;
        ks_adult ^= 1u;
        ks_note(ks_adult ? "Adult mode" : "Child mode", -1);
    }
}

static int ks_input(uint32_t down, uint32_t kd, const int32_t ev[NE])
{
    const uint32_t home = 1u << B_HOME, save = 1u << B_SAVE, edit = 1u << B_EDIT;
    int on_shelf = kids_mode == KIDS_HOME && kids_panel == KP_GLOBAL;
    if (ks_write.active) {
        if (kh_home && (ui.btn & home) && ev[EN_K1])
            ks_replay_left = clampi(ks_replay_left + clampi(ev[EN_K1], -KH_LIMIT, KH_LIMIT), -KH_LIMIT, KH_LIMIT);
        return 1;
    }
    if (ks_modal) {
        if ((down & home) || (ev[EN_K4] < 0 && ks_modal != KS_OPEN_DIRTY)) {
            ks_modal = KS_CLOSED;
            return 1;
        }
        if (ks_modal == KS_OPEN_DIRTY) {
            if (down & edit) {
                ks_modal = KS_CLOSED;
                ks_open(ks_target);
            } else if (down & save) {
                int free_slot = ks_empty_slot(ks_selected);
                if (free_slot < 0) ks_note("No free copy slot", -1);
                else ks_save_to(free_slot, ks_target + 1);
            }
        } else if (ks_modal == KS_PIN_REPLACE || ks_modal == KS_PIN_DELETE) {
            for (int k = 0; k < 10 && ks_pin_digits < 4; k++) if (kd & (1u << WHITE_K[k])) {
                ks_pin_entry = 10u * ks_pin_entry + (uint32_t)((k + 1) % 10);
                ks_pin_digits++;
            }
            if (down & save) {
                if (ks_pin_digits == 4 && ks_pin_token(ks_pin_entry) == ks_pin)
                    ks_modal = ks_modal == KS_PIN_DELETE ? KS_DELETE : KS_REPLACE;
                else ks_note("Incorrect PIN", -1);
                ks_pin_digits = 0;
                ks_pin_entry = 0;
            }
        } else if (down & save) {
            if (ks_modal == KS_DELETE) ks_start_write(ks_target, NULL, KS_DELETE_WRITE, 0);
            else ks_save_to(ks_target, 0);
        }
        return 1;
    }
    if (ui.btn & home || kh_home) return 0;
    if (down & save) { ks_save(); return 1; }
    if (on_shelf && !(down & (1u << B_GLO))) {
        if (ev[EN_K3]) ks_selected = (uint8_t)kids_wrap(ks_selected, ev[EN_K3], KS_SLOTS);
        if (ev[EN_K4] > 0) { ks_delete(); return 1; }
        if (down & edit) {
            if (!ks_slot_available(ks_selected)) return 1;
            if (!ks_slots[ks_selected].blob) ks_note("Empty slot selected", ks_selected);
            else {
                if (seq.rec || seq.rec_wait) { ks_note("End recording first", -1); return 1; }
                if (ui.dirty) { ks_target = ks_selected; ks_modal = KS_OPEN_DIRTY; }
                else ks_open(ks_selected);
            }
            return 1;
        }
    }
    if (down & (1u << B_GLO)) ks_glo();
    else if (down) ks_glo_count = 0;
    return 0;
}
