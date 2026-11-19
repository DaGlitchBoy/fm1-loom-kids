/* SPDX-License-Identifier: MPL-2.0 */
/* Emulator-only vertical slice. Included by ui.c after the original input helpers.
 * Musical state stays in proj/pats/seq; only presentation state lives here. */
#if defined(PLAT_HOST) || defined(PLAT_WEB)
static uint8_t kids_mode;                       /* 0 Studio, 1 Stage, 2 Steps, 3 Sounds */
static uint32_t kids_sig[8], kids_draws;
#include <stdlib.h>
#include "kids_music.h"

static uint8_t kids_scale[NTRK], kids_chord[NTRK], kids_root[NTRK];
static uint8_t kids_held_notes[NKEYS][SN], kids_note_users[128];
static uint8_t kids_rec_armed;
static uint32_t kids_overflow_seen, kids_full_until;

static void kids_category_sync(void);
static void kids_reset_banks(void);
static void kids_detach(void)
{
    /* Keep the current musical model, but remove its saved-slot association.
     * Studio also cannot autosave this copy. Its normal SAVE path asks before
     * replacing an existing slot. No project bytes or formats are changed. */
    if (proj_slot < NPROJ) ui.pick_save = proj_slot;
    proj_slot = NPROJ;
}
static void kids_set(int mode)
{
    if (mode && (ui.ask || lst.open || ui.held || ui.keys || recording() || ui.rec_armed)) {
        say_no("FINISH EDITING", "stop recording first");
        return;
    }
    engine_keys_off(inst_trk());
    if (mode) ui.nrem = 1;
    kids_mode = (uint8_t)mode;
    ui.page = P_STEPS;
    ui.card = ui.chordmode = ui.lockpg = ui.trickpg = 0;
    ui.held = 0;
    ui.btn_used |= ui.btn;
    if (sel_trk() >= NTRK) track_select(0);
    if (mode) {
        kids_detach();
        seq.kids_mode = 1;
        seq.rec_replace = 0;
        kids_category_sync();
    }
    ui_redraw();
}
enum { KC_KEYS, KC_BASS, KC_DRUMS, KC_PADS, KC_LEADS, KC_PLUCKS, KC_TEXTURES, KC_USER, KC_COUNT };
static uint8_t kids_category;
static const char *kids_sound_error;
static uint16_t kids_picks[NTRK][KC_COUNT];   /* index + 1, zero = no prior selection */
static const char *const KIDS_CATEGORY[] = {"KEYS", "BASS", "DRUMS", "PADS", "LEADS", "PLUCKS", "TEXTURES", "USER"};

typedef struct {
    tpat_t patterns[NPATT];
    int nlanes;
    lane_t lanes[];
} kids_bank_t;
static kids_bank_t *kids_other[NTRK];
/* LOOM stores scene mutes by kit order. Keep them associated with the
 * instrument track when an earlier track joins or leaves that order. */
static uint16_t kids_kit_mutes[NSCENE + 1][NTRK];

static void kids_remap_kit_mutes(int restore)
{
    for (int scene = 0; scene <= NSCENE; scene++) {
        volatile uint16_t *mutes = scene == NSCENE ? seq.kmute : pats.scene[scene].kmute;
        if (restore)
            for (int slot = 0; slot < NKITM; slot++) mutes[slot] = 0;
        for (int t = 0; t < NTRK; t++) {
            int slot = kit_slot(t);
            if (slot < 0) continue;
            if (restore) mutes[slot] = kids_kit_mutes[scene][t];
            else kids_kit_mutes[scene][t] = mutes[slot];
        }
    }
}


static void kids_reset_banks(void)
{
    kids_sound_error = NULL;
    memset(kids_scale, 0, sizeof kids_scale);
    memset(kids_chord, 0, sizeof kids_chord);
    memset(kids_root, 60, sizeof kids_root);
    memset(kids_held_notes, 0, sizeof kids_held_notes);
    memset(kids_note_users, 0, sizeof kids_note_users);
    kids_rec_armed = 0;
    kids_overflow_seen = kids_full_until = 0;
    seq.kids_mode = 0;
    seq.rec_overflow = 0;
    for (int t = 0; t < NTRK; t++) {
        free(kids_other[t]);
        kids_other[t] = NULL;
    }
    memset(kids_picks, 0, sizeof kids_picks);
    memset(kids_kit_mutes, 0, sizeof kids_kit_mutes);
}

/* Keep per-hit edits referenced by inactive drum lanes alive in the shared hit pool. */
static uint64_t kids_stashed_hits(void)
{
    uint64_t used = 0;
    for (int t = 0; t < NTRK; t++) {
        const kids_bank_t *bank = kids_other[t];
        if (!bank) continue;
        for (int i = 0; i < bank->nlanes; i++) if (lane_is_hit(bank->lanes[i].param))
            for (int step = 0; step < NSTEP; step++)
                if (bank->lanes[i].val[step] < NHIT)
                    used |= 1ull << bank->lanes[i].val[step];
    }
    return used;
}

/* Both encodings use the same step bytes. Swap a track's entire bank before
 * changing its engine. Preflight storage so failure leaves the track intact. */
static int kids_swap_bank(int track)
{
    kids_bank_t *other = kids_other[track];
    int own = 0, free_lanes = 0;
    for (int i = 0; i < NLANE; i++) {
        if (!pats.lane[i].used) free_lanes++;
        else if (pats.lane[i].trk == track) own++;
    }
    if (other && other->nlanes > free_lanes + own) {
        say_no("PATTERN LOCKS FULL", "free some locks first");
        return 0;
    }
    kids_bank_t *stash = malloc(sizeof *stash + (size_t)own * sizeof(lane_t));
    if (!stash) {
        say_no("MEMORY FULL", "sound kept");
        return 0;
    }
    memcpy(stash->patterns, pats.pat[track], sizeof stash->patterns);
    stash->nlanes = 0;
    for (int i = 0; i < NLANE; i++) if (pats.lane[i].used && pats.lane[i].trk == track) {
        stash->lanes[stash->nlanes++] = pats.lane[i];
        lane_clear(pats.lane, i);
    }
    if (other) {
        memcpy(pats.pat[track], other->patterns, sizeof other->patterns);
        for (int i = 0; i < other->nlanes; i++) {
            const lane_t *lane = &other->lanes[i];
            int slot = lane_alloc(pats.lane, track, lane->pat, lane->param);
            memcpy(pats.lane[slot].val, lane->val, sizeof lane->val);
        }
    } else {
        for (int p = 0; p < NPATT; p++) {
            tpat_t *pattern = &pats.pat[track][p];
            memset(pattern->st, 0, sizeof pattern->st);
        }
    }
    /* Keep the current loop geometry, including changes made in the other bank. */
    for (int p = 0; p < NPATT; p++) {
        tpat_t *pattern = &pats.pat[track][p];
        const tpat_t *current = &stash->patterns[p];
        pattern->len = current->len;
        pattern->lstart = current->lstart;
        pattern->lend = current->lend;
        pattern->scale = current->scale;
    }
    kids_other[track] = stash;
    free(other);
    return 1;
}

static int kids_role(int role)
{
    switch (role) {
    case RL_BASS: return KC_BASS;
    case RL_KEYS: case RL_CHORD: case RL_STAB: return KC_KEYS;
    case RL_PAD: case RL_STRINGS: case RL_VOICE: return KC_PADS;
    case RL_LEAD: case RL_BRASS: return KC_LEADS;
    case RL_PLUCK: case RL_BELL: return KC_PLUCKS;
    default: return KC_TEXTURES;
    }
}
static void kids_key_release(int key)
{
    for (int i = 0; i < SN && kids_held_notes[key][i]; i++) {
        int pitch = kids_held_notes[key][i];
        if (--kids_note_users[pitch] == 0) engine_key(sel_trk(), pitch, 0, 0);
    }
    memset(kids_held_notes[key], 0, sizeof kids_held_notes[key]);
}

static void kids_notes_off(void)
{
    for (int key = 0; key < NKEYS; key++) kids_key_release(key);
}

static void kids_remember_chord(int track)
{
    if (!is_drum(track))
        ui.nrem = (uint8_t)kids_voice_notes(kids_root[track], kids_scale[track], kids_chord[track], ui.rem);
}

static void kids_key_press(int key)
{
    int track = sel_trk();
    uint8_t *notes = kids_held_notes[key];
    int n;
    if (is_drum(track)) {
        notes[0] = (uint8_t)key_sound(key);
        ui.dsound = (uint8_t)(notes[0] - 1);
        n = 1;
    } else {
        kids_root[track] = (uint8_t)key_note(key);
        n = kids_voice_notes(kids_root[track], kids_scale[track], kids_chord[track], notes);
        memcpy(ui.rem, notes, (size_t)n);
        ui.nrem = (uint8_t)n;
    }
    for (int i = 0; i < n; i++) {
        int pitch = notes[i];
        /* A repeated drum hit retriggers, but only the last owner releases it. */
        if (kids_note_users[pitch]++ == 0 || is_drum(track)) engine_key(track, pitch, 1, 100);
    }
}

static void kids_rec_stop(void)
{
    kids_rec_armed = 0;
    if (!seq.rec) return;
    seq_rec_finish_keys(&seq);
    rec_end();
}

static void kids_rec_start(void)
{
    int track = sel_trk();
    undo_pat(track, cur_pat(track), UNDO_REC_MS);
    seq.rec_target = (uint8_t)track;
    seq.rec_replace = 0;
    seq_rec(&seq, 1);
    mark_dirty();
}

static void kids_home_knobs(const int32_t ev[NE])
{
    static const uint8_t lengths[] = {16, 32, 64};
    int track = sel_trk();
    tpat_t *pattern = tpat(track);
    if (ev[EN_K1]) set_tp(track, TP_LEVEL, proj.trk[track].par[TP_LEVEL] + clampi(ev[EN_K1], -100, 100));
    if (ev[EN_K2]) {
        int delta = clampi(ev[EN_K2], -3, 3), index = 0;
        int length = pattern->len;
        while (index < 2 && lengths[index] < length) index++;
        if (length != lengths[index] && delta > 0) delta--;
        int chosen = lengths[clampi(index + delta, 0, 2)];
        if (chosen != length) {
            kids_rec_stop();
            undo_keep();
            pattern->len = (uint8_t)chosen;
            ui.bar = (uint8_t)clampi(ui.bar, 0, (chosen - 1) / 16);
            mark_dirty();
        }
    }
    if (is_drum(track)) return;
    kids_chord[track] = (uint8_t)clampi(kids_chord[track] + clampi(ev[EN_K3], -KC_VOICINGS, KC_VOICINGS), 0, KC_VOICINGS - 1);
    kids_scale[track] = (uint8_t)clampi(kids_scale[track] + clampi(ev[EN_K4], -KS_COUNT, KS_COUNT), 0, KS_COUNT - 1);
    if (ev[EN_K3] || ev[EN_K4]) kids_remember_chord(track);
}

static int kids_sound_valid(int category, int i)
{
    if (category == KC_DRUMS) return i < NFKITS || user_kit_name[i - NFKITS][0];
    int engine = preset_engine(i);
    if (engine < 0 || engine == EG_DRUMLINE || engine == EG_MIDI) return 0;
    return i < NPRESETS ? kids_role(PRESETS[i].role) == category : category == KC_USER;
}

static int kids_catalog_size(int category)
{
    return category == KC_DRUMS ? NKITS : presets_all();
}

static int kids_sound_current(int category, int i)
{
    int track = sel_trk();
    if (category == KC_DRUMS)
        return is_drum(track) && ((i == 0 && name_is("kit", proj.trk[track].name)) ||
            name_is(i < NFKITS ? kit_name(i) : user_kit_name[i - NFKITS], proj.trk[track].name));
    return preset_engine(i) == proj.trk[track].par[TP_ENGINE] &&
        name_is(i < NPRESETS ? PRESETS[i].name : user_presets[i - NPRESETS].name, proj.trk[track].name);
}

static void kids_category_sync(void)
{
    for (int category = 0; category < KC_COUNT; category++) {
        int previous = (int)kids_picks[sel_trk()][category] - 1;
        if (previous >= 0 && kids_sound_current(category, previous)) {
            kids_category = (uint8_t)category;
            return;
        }
    }
    kids_category = is_drum(sel_trk()) ? KC_DRUMS : KC_KEYS;
    for (int category = 0; category < KC_COUNT; category++)
        for (int i = 0; i < kids_catalog_size(category); i++)
            if (kids_sound_valid(category, i) && kids_sound_current(category, i)) {
                kids_category = (uint8_t)category;
                kids_picks[sel_trk()][category] = (uint16_t)(i + 1);
                return;
            }
}

static int kids_sound_count(int *position)
{
    int n = 0;
    *position = 0;
    int choice = (int)kids_picks[sel_trk()][kids_category] - 1;
    for (int i = 0; i < kids_catalog_size(kids_category); i++) if (kids_sound_valid(kids_category, i)) {
        n++;
        if (i == choice && kids_sound_current(kids_category, i)) *position = n;
    }
    return n;
}

static int kids_apply_sound(int category, int index)
{
    int track = sel_trk(), drum = category == KC_DRUMS;
    int changes_type = drum != is_drum(track);
    if (drum && changes_type) {
        int active_kits = 0;
        for (int t = 0; t < NTRK; t++) active_kits += is_drum(t);
        if (active_kits >= NDL) {
            kids_sound_error = "Three drum tracks in use";
            return 0;
        }
    }
    uint8_t kit[KIT_LEN];
    int dev = 0;
    if (drum) {
        if (index < NFKITS) kit_factory(index, kit);
        else if ((dev = kit_load(index - NFKITS, kit)) < 0) return 0;
    }
    if (changes_type) {
        kids_rec_stop();
        if (!kids_swap_bank(track)) {
            kids_sound_error = "Sound kept: no pattern space";
            return 0;
        }
        kids_remap_kit_mutes(0);
    }
    kids_notes_off();
    kids_sound_error = NULL;
    if (drum) kit_apply(track, kit, dev);
    else preset_apply(track, index);
    if (changes_type) kids_remap_kit_mutes(1);
    kids_remember_chord(track);
    kids_category = (uint8_t)category;
    kids_picks[track][category] = (uint16_t)(index + 1);
    return 1;
}

static int kids_wrap(int index, int delta, int count)
{
    return (index + delta % count + count) % count;
}

static void kids_sound_step(int delta)
{
    int position, count = kids_sound_count(&position);
    if (!count) return;
    int target = kids_wrap(position ? position - 1 : delta > 0 ? count - 1 : 0, delta, count);
    for (int i = 0; i < kids_catalog_size(kids_category); i++)
        if (kids_sound_valid(kids_category, i) && target-- == 0) {
            kids_apply_sound(kids_category, i);
            return;
        }
}

static void kids_category_step(int delta)
{
    int available[KC_COUNT], count = 0, position = 0;
    for (int category = 0; category < KC_COUNT; category++)
        for (int i = 0; i < kids_catalog_size(category); i++)
            if (kids_sound_valid(category, i)) {
                if (category == kids_category) position = count;
                available[count++] = category;
                break;
            }
    if (!count) return;
    int category = available[kids_wrap(position, delta, count)];
    if (category == kids_category) return;
    int previous = (int)kids_picks[sel_trk()][category] - 1;
    if (previous >= 0 && kids_sound_valid(category, previous)) {
        kids_apply_sound(category, previous);
        return;
    }
    for (int i = 0; i < kids_catalog_size(category); i++)
        if (kids_sound_valid(category, i)) {
            kids_apply_sound(category, i);
            return;
        }
}

static void kids_track(int d)
{
    if (d % NTRK == 0) return;
    kids_sound_error = NULL;
    kids_rec_stop();
    kids_notes_off();
    track_select(((sel_trk() + d % NTRK) % NTRK + NTRK) % NTRK);
    kids_remember_chord(sel_trk());
    kids_category_sync();
}
static int kids_input(void)
{
    if (!kids_mode) return 0;
    uint32_t btn = plat_buttons(), keys = plat_keys();
    uint32_t down = btn & ~ui.btn, kd = keys & ~ui.keys, ku = ui.keys & ~keys;
    int32_t ev[NE];
    for (int i = 0; i < NE; i++) ev[i] = plat_enc(i);
    for (int i = 0; i < NKEYS; i++)
        if (ku & (1u << i)) kids_key_release(i);
    if (kids_overflow_seen != seq.rec_overflow) {
        kids_overflow_seen = seq.rec_overflow;
        kids_full_until = plat_ms() + 800;
    }
    ui.btn = btn; ui.keys = keys; ui.btn_used = btn;
    /* SEL is reserved for delay in a later delivery. */
    if (down & ((1u << B_HOME) | (1u << B_SEQ) | (1u << B_EDIT))) {
        if (!(down & (1u << B_HOME)) && (down & (1u << B_SEQ))) kids_rec_stop();
        kids_notes_off();
        kids_mode = down & (1u << B_HOME) ? 1 : down & (1u << B_SEQ) ? 2 : 3;
    }
    if ((down & (1u << B_REC)) && kids_mode != 2) {
        if (kids_rec_armed || seq.rec) kids_rec_stop();
        else if (playing()) kids_rec_start();
        else kids_rec_armed = 1;
    }
    if (down & (1u << B_PLAY)) {
        if (playing()) {
            kids_rec_stop();
            kids_notes_off();
        } else if (kids_rec_armed) {
            kids_rec_armed = 0;
            kids_rec_start();
        }
        uint32_t held = ui.btn;
        ui.btn = 1u << B_PLAY;
        play_press();
        ui.btn = held;
    }
    int d = (int)ev[EN_SELECT];
    if (d) kids_track(d);
    d = !!(down & (1u << B_OCTUP)) - !!(down & (1u << B_OCTDN));
    if (d) {
        kids_notes_off();
        int previous = ui.oct;
        ui.oct = (int8_t)clampi(ui.oct + d, -4, 4);
        for (int track = 0; track < NTRK; track++)
            kids_root[track] = (uint8_t)clampi(kids_root[track] + (ui.oct - previous) * 12, 1, 127);
        kids_remember_chord(sel_trk());
    }

    if (kids_mode == 1) kids_home_knobs(ev);
    if (kids_mode == 2)
        ui.bar = (uint8_t)clampi(ui.bar + clampi(ev[EN_K2], -3, 3), 0, (tpat(sel_trk())->len - 1) / 16);
    if (kids_mode == 2 && ev[EN_K3]) {
        if (is_drum(sel_trk())) ui.dsound = (uint8_t)clampi(ui.dsound + ev[EN_K3], 0, DL_NSOUND - 1);
        else {
            int track = sel_trk();
            kids_root[track] = (uint8_t)clampi(kids_root[track] + clampi(ev[EN_K3], -127, 127), 1, 127);
            kids_remember_chord(track);
        }
    }
    if (ev[EN_PRESET]) kids_category_step(ev[EN_PRESET]);
    if (ev[EN_ALGO]) kids_sound_step(ev[EN_ALGO]);
    for (int i = 0; i < NKEYS; i++) if (kd & (1u << i)) {
        int w = ordinal(WHITE_K, 16, i), b = ordinal(BLACK_K, 11, i);
        if (kids_mode == 2) {
            if (w >= 0 && step_shown(w) < tpat(sel_trk())->len) step_toggle(w);
            else if (b >= 0 && is_drum(sel_trk())) ui.dsound = (uint8_t)b;
        } else kids_key_press(i);
    }
    return 1;
}
static void kids_leds(void)
{
    uint32_t buttons = 1u << (kids_mode == 1 ? B_HOME : kids_mode == 2 ? B_SEQ : B_EDIT);
    uint32_t keys = 0, mid = 0;
    if (playing()) buttons |= 1u << B_PLAY;
    if (seq.rec || kids_rec_armed) buttons |= 1u << B_REC;
    if (proj.lights) {
        keys = ui.keys;
        if (kids_mode == 2) {
            const tpat_t *tp = tpat(sel_trk());
            for (int i = 0; i < 16; i++) {
                int st = ui.bar * 16 + i;
                if (st < tp->len && (is_drum(sel_trk()) ? kit_has(&tp->st[st], ui.dsound + 1) : step_on(&tp->st[st], sel_trk())))
                    mid |= 1u << WHITE_K[i];
                if (playing() && eng_step[sel_trk()] == st) keys |= 1u << WHITE_K[i];
            }
            if (is_drum(sel_trk())) keys |= 1u << BLACK_K[ui.dsound % 11];
        }
    }
    loom_lights(buttons, keys, proj.lights == 1, mid & ~keys, 0);
    plat_lights_mid(0, mid & ~keys);
}
static void kids_text(int x, int y, const char *s)
{
    cv_text(x, y, &LFONT_XS, s, RGB(235, 235, 238));
}
static void kids_number(int x, int y, int n)
{
    char s[12];
    num_s((uint32_t)n, s);
    kids_text(x, y, s);
}
/* Small procedural instruments, drawn on a 24-unit grid. No bitmap assets. */
#define KIDS_BG RGB(12, 18, 30)
#define KIDS_PANEL RGB(26, 36, 54)
#define KIDS_WHITE RGB(242, 246, 252)
static void kids_pixel(int x, int y, int size, int px, int py, int w, int h, uint16_t ink)
{
    cv_rect(x + px * size / 24, y + py * size / 24,
            (w * size + 23) / 24, (h * size + 23) / 24, ink);
}
static void kids_icon(int kind, int x, int y, int size, uint16_t ink)
{
#define KP(a,b,w,h) kids_pixel(x,y,size,a,b,w,h,ink)
    switch (kind % 8) {
    case 0: /* drum and two sticks */
        KP(3,8,18,3); KP(3,17,18,3); KP(3,10,3,9); KP(18,10,3,9);
        KP(8,11,2,6); KP(14,11,2,6); KP(5,2,3,4); KP(8,5,3,3); KP(17,1,3,5); break;
    case 1: /* bass neck, strings and body */
        KP(15,1,5,4); KP(13,5,3,8); KP(10,11,9,3); KP(5,14,15,6);
        KP(8,20,9,2); KP(3,16,3,3); KP(11,10,2,10); break;
    case 2: /* keyboard */
        KP(2,5,20,3); KP(2,18,20,3); KP(2,8,2,10); KP(20,8,2,10);
        KP(7,8,2,10); KP(13,8,2,10); KP(5,7,3,6); KP(11,7,3,6); KP(17,7,3,6); break;
    case 3: /* cloud / pad */
        KP(7,4,9,3); KP(4,7,15,3); KP(2,10,20,8); KP(5,18,14,3); break;
    case 4: /* bell */
        KP(10,2,4,3); KP(7,5,10,3); KP(5,8,14,8); KP(3,16,18,3); KP(10,20,4,2); break;
    case 5: /* plucked sparkle */
        KP(10,2,4,20); KP(2,10,20,4); KP(7,7,10,10); KP(5,5,3,3); KP(17,17,3,3); break;
    case 6: /* horn */
        KP(2,8,10,3); KP(10,5,4,14); KP(14,3,4,18); KP(18,1,4,22);
        KP(4,11,3,8); KP(7,17,4,3); break;
    case 7: /* two notes */
        KP(8,3,13,3); KP(8,6,3,12); KP(18,6,3,9); KP(3,16,8,5); KP(13,13,8,5); break;
    }
#undef KP
}
static void kids_label(int x, int y, const loom_font_t *font, const char *s, uint16_t ink, int width)
{
    char out[40];
    int n = 0;
    while (s[n] && n < (int)sizeof out - 1) { out[n] = s[n]; n++; }
    out[n] = 0;
    while (n && text_w(font, out) > width) out[--n] = 0;
    cv_text(x, y, font, out, ink);
}
static void kids_sound_name(char *name)
{
    memcpy(name, proj.trk[sel_trk()].name, NAME_LEN);
    name[NAME_LEN] = 0;
    if (!name[0]) s_cat(name, is_drum(sel_trk()) ? "Loom" : "Sound");
    if (is_drum(sel_trk()) && name_is("kit", name)) s_cat(name, "Loom kit");
}
static int kids_lit(int t)
{
    if (t == sel_trk() && kids_mode != 2 && ui.keys) return 1;
    return playing() && !(seq.mute & (1u << t)) && step_on(&tpat(t)->st[eng_step[t] % NSTEP], t);
}
static void kids_draw(void)
{
    static const uint8_t stage_h[] = {28, 64, 64, 20, 24, 40};
    static const uint8_t steps_h[] = {28, 24, 29, 29, 29, 29, 32, 40};
    static const uint8_t sound_h[] = {28, 32, 64, 38, 38, 40};
    const uint8_t *heights = kids_mode == 1 ? stage_h : kids_mode == 2 ? steps_h : sound_h;
    int bands = kids_mode == 2 ? 8 : 6, trk = sel_trk(), y = 0;
    int now = playing() ? eng_step[trk] : -1, pos, count = kids_sound_count(&pos);
    uint16_t ink = THEMES[THEME_BW].ink[trk];
    char name[NAME_LEN + 1]; kids_sound_name(name);
    const tpat_t *tp = tpat(trk);
    int rec_status = seq.rec ? 2 : kids_rec_armed ? 1 : 0;
    int full = (int32_t)(kids_full_until - plat_ms()) > 0;
    for (int b = 0; b < bands; b++) {
        uint32_t h = hash_u(2166136261u, kids_mode | trk << 4);
        if (b == 0) {
            h = hash_u(h, rec_status); h = hash_u(h, full);
            h = hash_u(h, now + 1); h = hash_u(h, cur_pat(trk)); h = hash_u(h, cued_pat(trk));
        } else if (kids_mode == 1 && (b == 1 || b == 2)) {
            h = hash_u(h, seq.mute);
            for (int t = (b - 1) * 4; t < b * 4; t++) h = hash_u(h, kids_lit(t));
        } else if (kids_mode == 2 && b >= 2 && b <= 5) {
            h = hash_u(h, now + 1); h = hash_u(h, ui.bar); h = hash_u(h, tp->len);
            for (int i = 0; i < 4; i++) {
                int st = ui.bar * 16 + (b - 2) * 4 + i;
                h = hash_u(h, is_drum(trk) ? kit_has(&tp->st[st], ui.dsound + 1) : step_on(&tp->st[st], trk));
            }
        } else {
            h = hash_s(h, name);
            if (kids_sound_error) h = hash_s(h, kids_sound_error);
            h = hash_u(h, kids_category); h = hash_u(h, pos);
            h = hash_u(h, proj.trk[trk].par[TP_LEVEL]); h = hash_u(h, tp->len);
            h = hash_u(h, kids_chord[trk]); h = hash_u(h, kids_scale[trk]);
            h = hash_u(h, ui.bar); h = hash_u(h, (uint8_t)ui.oct); h = hash_u(h, ui.dsound); h = hash_u(h, ui.rem[0]);
            h = hash_u(h, proj.tempo_x10); h = hash_u(h, cued_pat(trk));
            if (kids_mode == 3 && (b == 2 || b == 4)) h = hash_u(h, ui.keys);
        }
        if (kids_sig[b] == h) { y += heights[b]; continue; }
        kids_sig[b] = h; kids_draws++;
        cv_begin(240, heights[b], KIDS_BG);
        if (b == 0) {
            kids_label(6, 5, &LFONT_B, kids_mode == 1 ? "LOOM KIDS" : kids_mode == 2 ? "STEPS" : "SOUNDS", KIDS_WHITE, 104);
            kids_text(116, 7, "P"); kids_number(125, 7, cur_pat(trk) + 1);
            if (cued_pat(trk) != cur_pat(trk)) { kids_text(140, 7, ">"); kids_number(149, 7, cued_pat(trk) + 1); }
            if (playing()) {
                for (int i = 0; i < 8; i++) cv_rect(170 + i, 7 + i / 2, 1, 12 - i, ink);
            } else cv_rect(170, 9, 8, 8, KIDS_WHITE);
            if (full || rec_status)
                kids_label(185, 7, &LFONT_XS, full ? "FULL" : rec_status == 2 ? "REC" : "ARMED", RGB(255, 122, 140), 48);
            else
                for (int i = 0; i < 4; i++) cv_rect(185 + i * 7, 11, 4, 4, now >= 0 && now % 4 == i ? ink : KIDS_PANEL);

            cv_rect(6, 27, 228, 1, KIDS_PANEL);
        } else if (b == bands - 1) {
            if (kids_mode == 1) {
                static const char *const chord_names[] = {"Single", "In scale", "Major", "Minor", "Power", "Min 7", "Octaves", "Maj 7"};
                static const char *const scale_names[] = {"Chromatic", "Major", "Minor", "Maj pent", "Min pent", "Dorian", "Egyptian"};
                kids_text(6, 3, "K1 Volume");
                kids_text(66, 3, "K2 Length");
                kids_text(126, 3, "K3 Chord");
                kids_text(186, 3, "K4 Scale");
                kids_number(6, 20, proj.trk[trk].par[TP_LEVEL]);
                kids_number(66, 20, tp->len);
                kids_label(126, 20, &LFONT_XS, is_drum(trk) ? "-" : chord_names[kids_chord[trk]], ink, 54);
                kids_label(186, 20, &LFONT_XS, is_drum(trk) ? "-" : scale_names[kids_scale[trk]], ink, 51);
            } else if (kids_mode == 3) {
                char octave[4] = {ui.oct < 0 ? '-' : '+', (char)('0' + (ui.oct < 0 ? -ui.oct : ui.oct)), 0};
                kids_text(6, 3, "SELECT");
                kids_text(65, 3, "PRESETS");
                kids_text(131, 3, "ALGORITHM");
                kids_text(207, 3, "OCT");
                kids_number(6, 20, trk + 1);
                kids_label(65, 19, &LFONT_XS, KIDS_CATEGORY[kids_category], ink, 61);
                kids_number(131, 20, pos);
                kids_text(207, 20, is_drum(trk) ? "KIT" : octave);
            } else {
                kids_text(6, 4, "SELECT track");
                kids_text(92, 4, "PRESETS group");
                kids_text(6, 20, "ALGORITHM sound");
                kids_text(134, 20, kids_sound_error ? "Sound kept" : "K2 bar / K3 note");
            }
        } else if (kids_mode == 1) {
            if (b == 1 || b == 2) for (int i = 0; i < 4; i++) {
                int t = (b - 1) * 4 + i, x = 4 + 59 * i, lit = kids_lit(t);
                uint16_t c = THEMES[THEME_BW].ink[t];
                cv_rect(x, 4, 55, 56, t == trk ? c : KIDS_PANEL);
                cv_rect(x + 2, 6, 51, 52, col_mix(KIDS_BG, c, t == trk ? 4 : 1));
                cv_rect(x + 2, 6, 51, t < 4 ? 3 : 1, c);
                kids_number(x + 5, 10, t + 1);
                kids_icon(t, x + 16, 20 - (lit ? 2 : 0), 27, c);
                if (lit) cv_rect(x + 15, 51, 27, 3, c);
                if (seq.mute & (1u << t)) { cv_line(x + 15, 47, x + 43, 20, c); }
            } else if (b == 3) {
                cv_rect(6, 3, 3, 14, ink);
                kids_number(14, 2, trk + 1);
                kids_label(29, 1, &LFONT_B, kids_sound_error ? kids_sound_error : name, ink, 204);
            } else {
                char octave[4] = {ui.oct < 0 ? '-' : '+', (char)('0' + (ui.oct < 0 ? -ui.oct : ui.oct)), 0};
                kids_label(6, 5, &LFONT_XS, KIDS_CATEGORY[kids_category], ink, 82);
                kids_text(87, 5, "REC overdub");
                kids_text(180, 5, "OCT");
                kids_text(208, 5, is_drum(trk) ? "-" : octave);
            }
        } else if (kids_mode == 2) {
            if (b == 1) {
                kids_icon(trk, 6, 2, 20, ink);
                kids_label(31, 4, &LFONT_XS, name, ink, 130);
                kids_label(170, 4, &LFONT_XS, is_drum(trk) ? DL_SOUND_NAME[ui.dsound] : "WHITE keys", KIDS_WHITE, 65);
            } else if (b <= 5) for (int i = 0; i < 4; i++) {
                int st = ui.bar * 16 + (b - 2) * 4 + i, x = 5 + 59 * i;
                int on = is_drum(trk) ? kit_has(&tp->st[st], ui.dsound + 1) : step_on(&tp->st[st], trk);
                cv_rect(x, 2, 53, 24, st == now ? KIDS_WHITE : on ? ink : KIDS_PANEL);
                if (st == now) cv_rect(x + 2, 4, 49, 20, on ? ink : KIDS_BG);
                char value[12]; num_s((uint32_t)st + 1, value);
                cv_text(x + 7, 5, &LFONT_B, value, on ? KIDS_BG : st < tp->len ? KIDS_WHITE : RGB(97, 110, 130));
                if (on) cv_rect(x + 39, 11, 5, 5, KIDS_BG);
            } else {
                for (int i = 0; i < 4; i++) {
                    cv_rect(6 + i * 43, 5, 38, 22, ui.bar == i ? ink : KIDS_PANEL);
                    char digit[4]; num_s((uint32_t)i + 1, digit);
                    cv_text(21 + i * 43, 8, &LFONT_XS, digit, ui.bar == i ? KIDS_BG : KIDS_WHITE);
                }
                kids_text(182, 10, "OCT-/+");
            }
        } else { /* Sounds: categories, an instrument, a real preset name and a live preview keyboard. */
            if (b == 1) {
                cv_rect(6, 4, 228, 24, col_mix(KIDS_BG, ink, 3));
                kids_text(12, 9, "<");
                kids_label(30, 7, &LFONT_B, is_drum(trk) ? "DRUM KITS" : KIDS_CATEGORY[kids_category], ink, 140);
                kids_number(182, 9, pos); kids_text(196, 9, "/"); kids_number(206, 9, count);
            } else if (b == 2) {
                kids_icon(is_drum(trk) ? 0 : kids_category == KC_BASS ? 1 : kids_category == KC_KEYS ? 2 : kids_category == KC_PADS ? 3 :
                    kids_category == KC_LEADS ? 6 : kids_category == KC_PLUCKS ? 4 : 5, 94, ui.keys ? 3 : 7, 52, ink);
                kids_text(20, 27, "<"); kids_text(214, 27, ">");
            } else if (b == 3) {
                cv_rect(6, 1, 228, 32, KIDS_PANEL);
                kids_label(15, 8, &LFONT_B, kids_sound_error ? kids_sound_error : name, KIDS_WHITE, 211);
            } else {
                for (int i = 0; i < 16; i++) {
                    int black = ordinal(BLACK_K, 11, WHITE_K[i] + 1);
                    cv_rect(8 + i * 14, 0, 12, 20, (ui.keys & (1u << WHITE_K[i])) ? ink : KIDS_WHITE);
                    if (black >= 0) cv_rect(17 + i * 14, 0, 6, 11,
                        (ui.keys & (1u << BLACK_K[black])) ? ink : KIDS_BG);
                }
                kids_text(7, 22, "ALGORITHM sound"); kids_text(140, 22, "KEYS play");
            }
        }
        cv_blit(0, y); y += heights[b];
    }
}
#endif
