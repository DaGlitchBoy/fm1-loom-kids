/* SPDX-License-Identifier: MPL-2.0 */
/* Emulator-only vertical slice. Included by ui.c after the original input helpers.
 * Musical state stays in proj/pats/seq; only presentation state lives here. */
#if defined(PLAT_HOST) || defined(PLAT_WEB) || defined(LION_KIDS_DEVICE)
static uint8_t kids_mode;                       /* 0 Studio */
enum { KIDS_HOME = 1, KIDS_STEPS, KIDS_ENV, KIDS_PERFORMANCE, KIDS_LFO };
enum { KP_HOME, KP_REVERB, KP_DELAY, KP_ARP, KP_GLOBAL, KP_QUICK };
static uint8_t kids_panel;
static struct {
    int button, track, mode, panel;
    uint32_t at;
} kids_effect_return;
static uint8_t kids_shaping[NTRK][KD_COUNT];
static const char *const kids_lfo_targets[] = {"Volume", "Cutoff", "Drive"};
static const char *const kids_lfo_rates[KD_RATE_COUNT] = {
    "2 bars", "1 bar", "1/2", "1/4D", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"
};
static const char *const kids_lfo_waves[] = {"Sine", "Triangle", "Saw", "Square"};
static uint8_t kids_fx_on[NTRK][2], kids_fx_amount[NTRK][2];
static uint8_t kids_arp_on[NTRK], kids_arp_order[NTRK], kids_arp_rate[NTRK];
static uint8_t kids_metro, kids_delay_choice, kids_delay_actual;
static uint16_t kids_delay_tempo;
static const char *const kids_delay_names[] = {"1/16", "1/8T", "1/8", "1/8D", "1/4"};
static const uint8_t kids_delay_values[] = {45, 58, 83, 109, 122};
static const char *const kids_arp_names[] = {"Up", "Down", "Up-down", "Down-up", "Chaotic"};
static const char *const kids_rate_names[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T"};
static uint8_t kids_perf_page[NTRK], kids_cursor[NTRK], kids_seq_view, kids_perf_manual;
typedef struct {
    int track, pattern, step;
    int next_hit, next_velocity;
} kids_cell_t;
static kids_cell_t kids_cell;
static uint32_t kids_sig[8], kids_draws;
static uint32_t glo_overlay_down_at, glo_overlay_tap_at, glo_overlay_screen_at;
static uint8_t glo_overlay_taps, glo_overlay_armed, glo_overlay_screen, glo_overlay_painted;
#include <stddef.h>
#ifdef LION_KIDS_DEVICE
#include "kids_device_heap.c"
#else
#include <stdlib.h>
#endif
#include "kids_music.h"

static uint8_t kids_scale[NTRK], kids_chord[NTRK], kids_root[NTRK];
static uint8_t kids_held_notes[NKEYS][SN], kids_note_users[128];
static uint8_t kids_rec_armed;
static uint32_t kids_overflow_seen, kids_full_until;

static void kids_category_sync(void);
static void kids_reset_banks(void);
static void kids_init_panels(void);
static void ks_init(int startup);
static void kids_detach(void)
{
    /* Keep the current musical model, but remove its saved-slot association.
     * Studio also cannot autosave this copy. Its normal SAVE path asks before
     * replacing an existing slot. No project bytes or formats are changed. */
    if (proj_slot < NPROJ) ui.pick_save = proj_slot;
    proj_slot = NPROJ;
}
static void kids_enter(int startup)
{
    if (ui.ask || lst.open || ui.held || ui.keys || recording() || ui.rec_armed) {
        say_no("FINISH EDITING", "stop recording first");
        return;
    }
    engine_keys_off(inst_trk());
    ui.nrem = 1;
    kids_mode = KIDS_HOME;
    ui.page = P_STEPS;
    ui.card = ui.chordmode = ui.lockpg = ui.trickpg = 0;
    ui.held = 0;
    ui.btn_used |= ui.btn;
    if (sel_trk() >= NTRK) track_select(0);
    kids_detach();
    seq.kids_mode = 1;
    seq.rec_replace = 0;
    kids_category_sync();
    kids_init_panels();
    ks_init(startup);
    ui_redraw();
}
enum { KC_KEYS, KC_BASS, KC_DRUMS, KC_PADS, KC_LEADS, KC_PLUCKS, KC_TEXTURES, KC_USER, KC_COUNT };
static int kids_wet_category(int category) { return category == KC_KEYS || category == KC_PLUCKS; }
static uint8_t kids_category, kids_categories[NTRK];
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

static void kids_notes_off(void);
static void kids_remember_chord(int track);
static void kids_update_delay(void);
static int kids_page_count(int track);
static void kids_update_performance_page(void);
#include "kids_pixel_names.c"
#include "kids_history.c"

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
    kh_reset();
    kids_effect_return.button = 0;
    kids_sound_error = NULL;
    kids_send_override = 0;
    kids_cell.track = -1;
    memset(kids_perf_page, 0, sizeof kids_perf_page);
    memset(kids_cursor, 0, sizeof kids_cursor);
    glo_overlay_taps = glo_overlay_armed = glo_overlay_screen = glo_overlay_painted = 0;
    kids_seq_view = KIDS_PERFORMANCE;
    kids_perf_manual = 0;
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
    kh_bank_changed(other);
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
    if (seq.rec) {
        seq_rec_finish_keys(&seq);
        rec_end();
    }
    kh_end(&kh_take);
}

static void kids_rec_start(void)
{
    int track = sel_trk();
    kh_begin(&kh_take, KH_RECORD, "Recording", track, 0);
    seq.rec_target = (uint8_t)track;
    seq.rec_replace = 0;
    seq_rec(&seq, 1);
}

static void kids_set_length(int delta)
{
    static const uint8_t lengths[] = {16, 32, 64};
    tpat_t *pattern = tpat(sel_trk());
    int length = pattern->len;
    int index = 0;
    while (index < 2 && lengths[index] < length) index++;
    if (length != lengths[index] && delta > 0) delta--;
    int chosen = lengths[clampi(index + delta, 0, 2)];
    if (chosen == length) return;
    kids_rec_stop();
    undo_keep();
    pattern->len = (uint8_t)chosen;
    ui.bar = (uint8_t)clampi(ui.bar, 0, (chosen - 1) / 16);
    mark_dirty();
}

static void kids_mix_knobs(const int32_t ev[NE])
{
    int track = sel_trk();
    if (ev[EN_K1]) set_tp(track, TP_LEVEL, proj.trk[track].par[TP_LEVEL] + clampi(ev[EN_K1], -100, 100));
    if (ev[EN_K2]) kids_set_length(clampi(ev[EN_K2], -3, 3));
}

static void kids_home_knobs(const int32_t ev[NE])
{
    kids_mix_knobs(ev);
    int track = sel_trk();
    if (is_drum(track)) return;
    kids_chord[track] = (uint8_t)clampi(kids_chord[track] + clampi(ev[EN_K3], -KC_VOICINGS, KC_VOICINGS), 0, KC_VOICINGS - 1);
    kids_scale[track] = (uint8_t)clampi(kids_scale[track] + clampi(ev[EN_K4], -KS_COUNT, KS_COUNT), 0, KS_COUNT - 1);
    if (ev[EN_K3] || ev[EN_K4]) kids_remember_chord(track);
}

static int kids_delay_fits(int i)
{
    return !fx_delay_halves(kids_delay_values[i], 64, proj.tempo_x10 * 0.1f);
}

static int kids_delay_limit(void)
{
    int last = (int)(sizeof kids_delay_values / sizeof kids_delay_values[0]) - 1;
    while (last > 0 && !kids_delay_fits(last)) last--;
    return last;
}

static void kids_update_delay(void)
{
    kids_delay_actual = (uint8_t)clampi(kids_delay_choice, 0, kids_delay_limit());
    proj.trk[AUX_FX1].par[TP_E1] = kids_delay_values[kids_delay_actual];
    proj.trk[AUX_FX1].par[TP_E2] = 64;
    kids_delay_tempo = proj.tempo_x10;
    apply_fx(AUX_FX1);
}

static void kids_shape_set(int track, int parameter, int value)
{
    kids_shaping[track][parameter] = (uint8_t)value;
    engine_kids_dsp(track, parameter, value);
}

static const uint8_t kids_shape_defaults[KD_COUNT] = {
    [KD_ATTACK] = 16, [KD_HOLD] = 18, [KD_SUSTAIN] = 88, [KD_RELEASE] = 25,
    [KD_DEPTH] = KD_DEPTH_ZERO + 64, [KD_RATE] = KD_BAR, [KD_LOWPASS] = 28,
    [KD_DRIVE] = 24, [KD_CRUSH] = 16, [KD_CHORUS] = 32
};
static void kids_init_panels(void)
{
    kids_panel = KP_HOME;
    memset(kids_fx_on, 0, sizeof kids_fx_on);
    memset(kids_arp_on, 0, sizeof kids_arp_on);
    memset(kids_arp_order, 0, sizeof kids_arp_order);
    for (int t = 0; t < NTRK; t++) {
        kids_fx_amount[t][0] = 48;
        kids_fx_amount[t][1] = 52;
        kids_send_gain[t][0] = kids_send_gain[t][1] = 0;
        kids_arp_rate[t] = 3;
        for (int p = 0; p < KD_COUNT; p++) kids_shape_set(t, p, kids_shape_defaults[p]);
        proj.trk[t].par[TP_PLAYER] = PL_OFF;
        engine_kids_player(t, PL_OFF);
    }
    kids_send_override = 1;
    const int types[] = {FXT_DELAY, FXT_REVERB + RV_LUSH};
    for (int slot = 0; slot < 2; slot++) {
        uint8_t *par = proj.trk[AUX_FX1 + slot].par;
        if (par[TP_ENGINE] != types[slot])
            memcpy(par + TP_E1, slot ? RV_LUSH_DEFAULT : FXT_DEFAULT[FXT_DELAY], 4);
        par[TP_ENGINE] = (uint8_t)types[slot];
        par[TP_LEVEL] = 100;
        engine_set(AUX_FX1 + slot, TP_LEVEL, 100);
        if (!slot) {
            par[TP_SEND2] = 0;
            engine_set(AUX_FX1, TP_SEND2, 0);
        }
    }
    kids_delay_choice = 2;
    kids_update_delay();
    apply_fx(AUX_FX2);
    kids_metro = 0;
    engine_kids_metro(0);
}

static void kids_update_arp(int track)
{
    static const uint8_t order[] = {0, 1, 2, 3, 5};
    static const uint8_t rate[] = {0, 3, 5, 6, 7};
    const int params[][2] = {
        {TP_P1, rate[kids_arp_rate[track]]}, {TP_P2, order[kids_arp_order[track]]},
        {TP_P3, 1}, {TP_P4, 0}, {TP_AP1, 1}, {TP_AP2, 0}, {TP_AP3, 0}, {TP_AP4, 0}
    };
    for (unsigned i = 0; i < sizeof params / sizeof params[0]; i++) {
        int p = params[i][0], v = params[i][1];
        proj.trk[track].par[p] = (uint8_t)v;
        engine_set(track, p, v);
    }
}

static void kids_toggle_panel(int panel)
{
    int track = sel_trk();
    if (panel == KP_GLOBAL || (panel == KP_ARP && is_drum(track))) {
        kids_mode = KIDS_HOME;
        kids_panel = panel;
        return;
    }
    int slot = panel == KP_REVERB ? 1 : 0;
    uint8_t *enabled = panel == KP_ARP ? &kids_arp_on[track] : &kids_fx_on[track][slot];
    int on = !(*enabled && kids_mode == KIDS_HOME && kids_panel == panel);
    if (on != *enabled) {
        *enabled = (uint8_t)on;
        if (panel == KP_ARP) {
            kids_notes_off();
            kids_update_arp(track);
            proj.trk[track].par[TP_PLAYER] = on ? PL_ARP : PL_OFF;
            engine_kids_player(track, proj.trk[track].par[TP_PLAYER]);
        } else {
            kids_send_gain[track][slot] = on ? kids_fx_amount[track][slot] : 0;
        }
        mark_dirty();
    }
    if (kids_mode == KIDS_STEPS) kids_rec_stop();
    kids_mode = KIDS_HOME;
    kids_panel = on ? panel : KP_HOME;
}

static void kids_toggle_shaping(int parameter)
{
    int track = sel_trk();
    int mode = parameter == KD_EDIT_ON ? KIDS_HOME : parameter == KD_ENV_ON ? KIDS_ENV : KIDS_LFO;
    int panel = parameter == KD_EDIT_ON ? KP_QUICK : KP_HOME;
    int on = !(kids_shaping[track][parameter] && kids_mode == mode && kids_panel == panel);
    if (kids_mode == KIDS_STEPS) kids_rec_stop();
    if (on != kids_shaping[track][parameter]) {
        kids_shape_set(track, parameter, on);
        mark_dirty();
    }
    kids_mode = on ? mode : KIDS_HOME;
    kids_panel = on ? panel : KP_HOME;
}

static void kids_shape_knobs(int first, const int32_t ev[NE])
{
    static const uint8_t max[KD_COUNT] = {
        [KD_ATTACK] = 127, [KD_HOLD] = 127, [KD_SUSTAIN] = 127, [KD_RELEASE] = 127,
        [KD_TARGET] = KD_TARGET_COUNT - 1, [KD_DEPTH] = 2 * KD_DEPTH_ZERO,
        [KD_RATE] = KD_RATE_COUNT - 1, [KD_WAVE] = KD_WAVE_COUNT - 1,
        [KD_LOWPASS] = 127, [KD_DRIVE] = 127, [KD_CRUSH] = 127, [KD_CHORUS] = 127
    };
    int track = sel_trk();
    for (int k = 0; k < 4; k++) {
        if (!ev[EN_K1 + k]) continue;
        static const uint8_t lfo_knobs[] = {KD_DEPTH, KD_TARGET, KD_RATE, KD_WAVE};
        int p = first == KD_DEPTH ? lfo_knobs[k] : first + k;
        int v = clampi(kids_shaping[track][p] + clampi(ev[EN_K1 + k], -2 * KD_DEPTH_ZERO, 2 * KD_DEPTH_ZERO), 0, max[p]);
        if (v != kids_shaping[track][p]) {
            kids_shape_set(track, p, v);
            mark_dirty();
        }
    }
}

static void kids_panel_knobs(const int32_t ev[NE])
{
    int track = sel_trk();
    if (kids_panel == KP_HOME) {
        kids_home_knobs(ev);
    } else if (kids_panel == KP_QUICK) {
        kids_shape_knobs(KD_LOWPASS, ev);
    } else if (kids_panel == KP_GLOBAL) {
        if (ev[EN_K1]) set_tempo_x10(proj.tempo_x10 + clampi(ev[EN_K1], -180, 180) * 10);
        if (ev[EN_K2]) {
            kids_metro = (uint8_t)clampi(kids_metro + clampi(ev[EN_K2], -100, 100), 0, 100);
            engine_kids_metro(kids_metro);
            mark_dirty();
        }
        if (ev[EN_K3]) {
            tpat_t *pattern = tpat(track);
            int swing = clampi(pattern->groove + clampi(ev[EN_K3], -99, 99), 0, 99);
            if (swing != pattern->groove || proj.groove != GR_SWING) {
                if (proj.groove != GR_SWING) set_groove_type(GR_SWING);
                pattern->groove = (int8_t)swing;
                mark_dirty();
            }
        }
    } else if (kids_panel == KP_ARP) {
        if (is_drum(track)) return;
        kids_arp_order[track] = (uint8_t)clampi(kids_arp_order[track] + clampi(ev[EN_K1], -4, 4), 0, 4);
        kids_arp_rate[track] = (uint8_t)clampi(kids_arp_rate[track] + clampi(ev[EN_K2], -4, 4), 0, 4);
        if (ev[EN_K1] || ev[EN_K2]) {
            kids_update_arp(track);
            mark_dirty();
        }
        int32_t harmony[NE] = {0};
        harmony[EN_K3] = ev[EN_K3];
        harmony[EN_K4] = ev[EN_K4];
        kids_home_knobs(harmony);
    } else {
        int slot = kids_panel == KP_REVERB ? 1 : 0;
        int aux = AUX_FX1 + slot;
        uint8_t *par = proj.trk[aux].par;
        if (ev[EN_K1]) {
            kids_fx_amount[track][slot] = (uint8_t)clampi(kids_fx_amount[track][slot] + clampi(ev[EN_K1], -127, 127), 0, 127);
            kids_send_gain[track][slot] = kids_fx_on[track][slot] ? kids_fx_amount[track][slot] : 0;
        }
        if (slot) {
            par[TP_E2] = (uint8_t)clampi(par[TP_E2] + clampi(ev[EN_K2], -127, 127), 0, 127);
            par[TP_E1] = (uint8_t)clampi(par[TP_E1] + clampi(ev[EN_K3], -127, 127), 0, 127);
            par[TP_E3] = (uint8_t)clampi(par[TP_E3] + clampi(ev[EN_K4], -127, 127), 0, 127);
            if (ev[EN_K2] || ev[EN_K3] || ev[EN_K4]) apply_fx(aux);
        } else {
            par[TP_E3] = (uint8_t)clampi(par[TP_E3] + clampi(ev[EN_K2], -127, 127), 0, 127);
            par[TP_E4] = (uint8_t)clampi(par[TP_E4] + clampi(ev[EN_K4], -127, 127), 0, 127);
            if (ev[EN_K3]) kids_delay_choice = (uint8_t)clampi(kids_delay_actual + clampi(ev[EN_K3], -4, 4), 0, kids_delay_limit());
            if (ev[EN_K2] || ev[EN_K3] || ev[EN_K4]) kids_update_delay();
        }
        if (ev[EN_K1] || ev[EN_K2] || ev[EN_K3] || ev[EN_K4]) mark_dirty();
    }
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

static int kids_sound_current(int category, int i, int track)
{
    if (category == KC_DRUMS)
        return is_drum(track) && ((i == 0 && name_is("kit", proj.trk[track].name)) ||
            name_is(i < NFKITS ? kit_name(i) : user_kit_name[i - NFKITS], proj.trk[track].name));
    return preset_engine(i) == proj.trk[track].par[TP_ENGINE] &&
        name_is(i < NPRESETS ? PRESETS[i].name : user_presets[i - NPRESETS].name, proj.trk[track].name);
}

static int kids_find_category(int track)
{
    for (int category = 0; category < KC_COUNT; category++) {
        int previous = (int)kids_picks[track][category] - 1;
        if (previous >= 0 && kids_sound_current(category, previous, track))
            return category;
    }
    for (int category = 0; category < KC_COUNT; category++)
        for (int i = 0; i < kids_catalog_size(category); i++)
            if (kids_sound_valid(category, i) && kids_sound_current(category, i, track)) {
                kids_picks[track][category] = (uint16_t)(i + 1);
                return category;
            }
    return is_drum(track) ? KC_DRUMS : KC_KEYS;
}

static void kids_category_sync(void)
{
    for (int track = 0; track < NTRK; track++) kids_categories[track] = (uint8_t)kids_find_category(track);
    kids_category = kids_categories[sel_trk()];
}

static int kids_sound_count(int *position)
{
    int n = 0;
    *position = 0;
    int choice = (int)kids_picks[sel_trk()][kids_category] - 1;
    for (int i = 0; i < kids_catalog_size(kids_category); i++) if (kids_sound_valid(kids_category, i)) {
        n++;
        if (i == choice && kids_sound_current(kids_category, i, sel_trk())) *position = n;
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
    if (changes_type) kids_rec_stop();
    kh_begin(&kh_edit, KH_SOUND, "Sound", track, 0);
    if (changes_type) {
        if (!kids_swap_bank(track)) {
            kh_end(&kh_edit);
            kids_sound_error = "Sound kept: no pattern space";
            return 0;
        }
        kids_remap_kit_mutes(0);
    }
    kids_notes_off();
    kids_sound_error = NULL;
    if (drum) kit_apply(track, kit, dev);
    else preset_apply(track, index);
    if (changes_type) {
        if (drum) {
            proj.trk[track].par[TP_PLAYER] = PL_OFF;
            engine_kids_player(track, PL_OFF);
        } else if (kids_arp_on[track]) {
            kids_update_arp(track);
            proj.trk[track].par[TP_PLAYER] = PL_ARP;
            engine_kids_player(track, PL_ARP);
        }
        kids_remap_kit_mutes(1);
        kids_cell.track = -1;
    }
    kids_remember_chord(track);
    if (kids_wet_category(category) != kids_wet_category(kids_category)) {
        for (int bus = 0; bus < 2; bus++) {
            kids_fx_on[track][bus] = (uint8_t)kids_wet_category(category);
            kids_send_gain[track][bus] = kids_fx_on[track][bus] ? kids_fx_amount[track][bus] : 0;
        }
    }
    kids_category = kids_categories[track] = (uint8_t)category;
    kids_picks[track][category] = (uint16_t)(index + 1);
    kh_end(&kh_edit);
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
    int target = clampi((position ? position - 1 : delta > 0 ? -1 : count) + clampi(delta, -count, count), 0, count - 1);
    if (position == target + 1) return;
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
    int category = available[clampi(position + clampi(delta, -count, count), 0, count - 1)];
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
    int target = clampi(sel_trk() + clampi(d, -NTRK, NTRK), 0, NTRK - 1);
    if (target == sel_trk()) return;
    kids_sound_error = NULL;
    kids_rec_stop();
    kids_notes_off();
    track_select(target);
    kids_perf_manual = 0;
    kids_remember_chord(sel_trk());
    kids_category_sync();
}
static int kids_page_count(int track)
{
    return clampi((tpat(track)->len + 15) / 16, 1, 4);
}

static int kids_selected_step(int track)
{
    if ((unsigned)track >= NTRK) return 0;
    return clampi(kids_cursor[track], 0, clampi(tpat(track)->len, 1, NSTEP) - 1);
}

static void kids_move_cursor(int delta)
{
    int track = sel_trk(), length = clampi(tpat(track)->len, 1, NSTEP);
    kids_cursor[track] = (uint8_t)kids_wrap(kids_selected_step(track), delta, length);
    ui.bar = kids_cursor[track] / 16;
}

static void kids_update_performance_page(void)
{
    int selected = sel_trk();
    for (int track = 0; track < NTRK; track++) {
        if (track == selected && (!playing() || kids_perf_manual))
            kids_perf_page[track] = (uint8_t)(kids_selected_step(track) / 16);
        else if (playing()) kids_perf_page[track] = eng_step[track] / 16;
        kids_perf_page[track] = (uint8_t)clampi(kids_perf_page[track], 0, kids_page_count(track) - 1);
    }
    if (playing() && !kids_perf_manual && kids_mode == KIDS_PERFORMANCE)
        kids_cursor[selected] = (uint8_t)clampi(kids_perf_page[selected] * 16 + kids_cursor[selected] % 16,
                                               0, clampi(tpat(selected)->len, 1, NSTEP) - 1);
}

/* 0: outside the loop, 1: empty, 2: occupied. The overview shows all kit hits. */
static int kids_performance_cell(int track, int column)
{
    int step = kids_perf_page[track] * 16 + column;
    const tpat_t *pattern = tpat(track);
    if (step >= pattern->len) return 0;
    return step_empty(&pattern->st[step]) ? 1 : 2;
}

static int kids_step_velocity(void)
{
    int track = sel_trk(), pattern = cur_pat(track), step = kids_selected_step(track);
    const tpat_t *tp = tpat(track);
    const step_t *st = &tp->st[step];
    int hit = is_drum(track) ? ui.dsound + 1 : 0;
    if (hit ? kit_has(st, hit) : !step_empty(st)) {
        int own = hit ? step_vel(pats.lane, track, pattern, step, hit - 1) : 0;
        return own ? own : st->vel;
    }
    if (kids_mode == KIDS_PERFORMANCE) return 0;
    return kids_cell.track == track && kids_cell.pattern == pattern && kids_cell.step == step &&
        kids_cell.next_hit == hit && kids_cell.next_velocity ? kids_cell.next_velocity : 100;
}

static int kids_has_step_note(const step_t *step, int track)
{
    return is_drum(track) ? kit_has(step, ui.dsound + 1) : !step_empty(step);
}

static void kids_edit_velocity(int delta)
{
    int velocity = kids_step_velocity();
    if (!velocity) return;
    int track = sel_trk(), step = kids_selected_step(track);
    step_t *st = &tpat(track)->st[step];
    int hit = is_drum(track) ? ui.dsound + 1 : 0;
    int target = clampi(velocity + delta, 1, 127);
    if (target == velocity) return;
    if (hit ? kit_has(st, hit) : !step_empty(st)) {
        undo_keep();
        if (hit) kit_vel(track, step, target - velocity);
        else st->vel = (uint8_t)target;
        mark_dirty();
    } else {
        kids_cell.track = track;
        kids_cell.pattern = cur_pat(track);
        kids_cell.step = step;
        kids_cell.next_hit = hit;
        kids_cell.next_velocity = target;
    }
}

static void kids_toggle_step(int column)
{
    int track = sel_trk(), pattern = cur_pat(track), step = step_shown(column);
    int hit = is_drum(track) ? ui.dsound + 1 : 0;
    if (step >= tpat(track)->len) return;
    int pending = 0;
    if (kids_cell.track == track && kids_cell.pattern == pattern && kids_cell.step == step && kids_cell.next_hit == hit)
        pending = kids_cell.next_velocity;
    step_toggle(column);
    kids_cursor[track] = (uint8_t)step;
    kids_cell.track = track;
    kids_cell.pattern = pattern;
    kids_cell.step = step;
    kids_cell.next_velocity = 0;
    step_t *st = &tpat(track)->st[step];
    if (hit && kit_has(st, hit)) {
        int own = step_vel(pats.lane, track, pattern, step, hit - 1);
        kit_vel(track, step, (pending ? pending : 100) - (own ? own : st->vel));
    } else if (!hit && pending && !step_empty(st)) st->vel = (uint8_t)pending;
}

static void kids_note_knobs(const int32_t ev[NE])
{
    int track = sel_trk(), index = kids_selected_step(track);
    tpat_t *pattern = tpat(track);
    step_t *step = &pattern->st[index];
    int has_note = kids_has_step_note(step, track);
    unsigned group = 0x500 + (unsigned)index * (NDRUM + 1u) + (is_drum(track) ? ui.dsound + 1u : 0u);
    if (ev[EN_K1] && kids_step_velocity()) {
        kh_begin(&kh_edit, KH_STEP, "Velocity", track, group);
        kids_edit_velocity(clampi(ev[EN_K1], -127, 127));
        kh_end(&kh_edit);
    }
    if (ev[EN_K2] && !is_drum(track) && (has_note || kids_mode == KIDS_STEPS)) {
        int delta = clampi(ev[EN_K2], -127, 127);
        kh_begin(&kh_edit, has_note ? KH_STEP : KH_SMALL, "Root note", track, 0x900u + (unsigned)index);
        if (has_note) {
            int lowest = 127, highest = 1;
            for (int i = 0; i < SN; i++) if (step->note[i]) {
                lowest = clampi(step->note[i], 1, lowest);
                highest = clampi(step->note[i], highest, 127);
            }
            delta = clampi(delta, 1 - lowest, 127 - highest);
            for (int i = 0; i < SN; i++)
                if (step->note[i]) step->note[i] = (uint8_t)(step->note[i] + delta);
            if (delta) mark_dirty();
        } else {
            kids_root[track] = (uint8_t)clampi(kids_root[track] + delta, 1, 127);
            kids_remember_chord(track);
        }
        kh_end(&kh_edit);
    }
    if (ev[EN_K3] && has_note) {
        int limit = step_len_code(clampi(pattern->len, 1, NSTEP) * 4);
        int length = clampi(step->len + clampi(ev[EN_K3], -limit, limit), 0, limit);
        if (step->len != length) {
            kh_begin(&kh_edit, KH_STEP, "Note length", track, 0xa00u + (unsigned)index);
            step->len = (uint8_t)length;
            mark_dirty();
            kh_end(&kh_edit);
        }
    }
}

#include "kids_shelf.c"

static int kids_history_input(uint32_t down, uint32_t btn, int32_t turn)
{
    const uint32_t home = 1u << B_HOME;
    if (down & home) {
        kh_home = 1;
        kh_home_since = plat_ms();
    }
    if (!kh_home) return 0;
    if (!(btn & home)) {
        if (!kh_visible) {
            kids_notes_off();
            kids_mode = KIDS_HOME;
            kids_panel = KP_HOME;
        }
        kh_home = kh_visible = 0;
    } else {
        if (turn || plat_ms() - kh_home_since >= KH_HOLD_MS) kh_visible = 1;
        if (turn) {
            kids_rec_stop();
            ks_replay_turn(turn);
        }
    }
    return 1;
}

static void kids_effect_buttons(uint32_t down)
{
    static const struct {
        int button, setting;
        const char *action;
    } effects[] = {
        {B_FX, KP_REVERB, "Reverb switch"}, {B_SEL, KP_DELAY, "Delay switch"},
        {B_ARP, KP_ARP, "Arp switch"}, {B_EDIT, KD_EDIT_ON, "Quick FX switch"},
        {B_ENV, KD_ENV_ON, "Envelope switch"}, {B_LFO, KD_LFO_ON, "LFO switch"}
    };
    if (down & (1u << B_GLO)) {
        kids_toggle_panel(KP_GLOBAL);
        return;
    }
    for (unsigned i = 0; i < sizeof effects / sizeof effects[0]; i++) {
        if (!(down & (1u << effects[i].button))) continue;
        int before_mode = kids_mode, before_panel = kids_panel, track = sel_trk();
        int button = effects[i].button;
        uint32_t now = plat_ms();
        kh_begin(&kh_edit, KH_SMALL, effects[i].action, track, 0);
        if (i < 3) kids_toggle_panel(effects[i].setting);
        else kids_toggle_shaping(effects[i].setting);
        kh_end(&kh_edit);
        int changed_focus = before_mode != kids_mode || before_panel != kids_panel;
        int from_effect = before_mode == KIDS_ENV || before_mode == KIDS_LFO ||
            (before_mode == KIDS_HOME && before_panel != KP_HOME && before_panel != KP_GLOBAL);
        if (changed_focus && kids_mode == KIDS_HOME && kids_panel == KP_HOME &&
            kids_effect_return.button == button + 1 && kids_effect_return.track == track &&
            now - kids_effect_return.at <= 300u) {
            kids_mode = (uint8_t)kids_effect_return.mode;
            kids_panel = (uint8_t)kids_effect_return.panel;
            kids_effect_return.button = 0;
        } else {
            kids_effect_return.button = changed_focus && from_effect ? button + 1 : 0;
            kids_effect_return.track = track;
            kids_effect_return.mode = before_mode;
            kids_effect_return.panel = before_panel;
            kids_effect_return.at = now;
        }
        return;
    }
}

static void kids_turn_knobs(const int32_t ev[NE])
{
    static const char *const home[][4] = {
        [KP_HOME] = {"Volume", "Length", "Chord", "Scale"},
        [KP_REVERB] = {"Reverb amount", "Reverb decay", "Room size", "Reverb tone"},
        [KP_DELAY] = {"Delay amount", "Delay repeats", "Delay rate", "Delay tone"},
        [KP_ARP] = {"Arp order", "Arp rate", "Chord", "Scale"},
        [KP_GLOBAL] = {"Tempo", "Metronome", "Swing"},
        [KP_QUICK] = {"Low-pass", "Drive", "Bitcrush", "Chorus"}
    };
    static const char *const env[] = {"Attack", "Hold", "Sustain", "Release"};
    static const char *const lfo[] = {"LFO depth", "LFO target", "LFO rate", "LFO wave"};
    if (kids_mode == KIDS_STEPS || kids_mode == KIDS_PERFORMANCE) return;
    const char *const *names = kids_mode == KIDS_HOME ? home[kids_panel] :
        kids_mode == KIDS_ENV ? env : lfo;
    int track = sel_trk();
    for (int k = 0; k < 4; k++) {
        if (!ev[EN_K1 + k] || !names[k]) continue;
        unsigned group = 1 + ((unsigned)kids_mode * 8 + kids_panel) * 4 + (unsigned)k;
        kh_begin(&kh_edit, KH_SMALL, names[k], track, group);
        int32_t turn[NE] = {0};
        turn[EN_K1 + k] = ev[EN_K1 + k];
        if (kids_mode == KIDS_HOME) kids_panel_knobs(turn);
        else if (kids_mode == KIDS_ENV) kids_shape_knobs(KD_ATTACK, turn);
        else kids_shape_knobs(KD_DEPTH, turn);
        if (kids_delay_tempo != proj.tempo_x10) kids_update_delay();
        kh_end(&kh_edit);
    }
}

static int glo_overlay_panel_input(uint32_t down, uint32_t up)
{
    const uint32_t glo = 1u << B_GLO;
    uint32_t now = plat_ms();
    if (glo_overlay_screen && now - glo_overlay_screen_at < 7000u) return 1;
    if (down & ~glo || ks_modal || kpix_open || kh_home) glo_overlay_taps = glo_overlay_armed = 0;
    if (down & glo && !ks_modal && !kpix_open && !kh_home) {
        glo_overlay_down_at = now;
        glo_overlay_armed = 1;
    }
    if (!(up & glo) || !glo_overlay_armed) return 0;
    glo_overlay_armed = 0;
    if (now - glo_overlay_down_at > 480u) {
        glo_overlay_taps = 0;
        return 0;
    }
    if (now - glo_overlay_tap_at > 850u) glo_overlay_taps = 0;
    glo_overlay_tap_at = now;
    if (++glo_overlay_taps != 7) return 0;
    glo_overlay_taps = 0;
    glo_overlay_screen = 1;
    glo_overlay_screen_at = now;
    kids_notes_off();
    return 1;
}

static int kids_input(void)
{
    if (!kids_mode) return 0;
    uint32_t btn = plat_buttons(), keys = plat_keys();
    uint32_t up = ui.btn & ~btn;
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
    if (kh_take.active && !seq.rec) kh_end(&kh_take);
    if (down || kd || ev[EN_SELECT] || ev[EN_PRESET] || ev[EN_ALGO]) kh_merge = 0;
    if (ev[EN_SELECT] || ev[EN_K1] || ev[EN_K2] || ev[EN_K3] || ev[EN_K4] ||
        (down & ((1u << B_HOME) | (1u << B_SEQ) | (1u << B_GLO))))
        kids_effect_return.button = 0;
    if (glo_overlay_panel_input(down, up)) return 1;
    if (ks_input(down, ev)) return 1;
    if (kids_history_input(down, btn, ev[EN_K1])) return 1;

    if (down & (1u << B_SEQ)) {
        if (kids_mode == KIDS_STEPS || kids_mode == KIDS_PERFORMANCE)
            kids_seq_view = kids_mode == KIDS_STEPS ? KIDS_PERFORMANCE : KIDS_STEPS;
        if (kids_seq_view == KIDS_STEPS) kids_rec_stop();
        kids_notes_off();
        kids_mode = kids_seq_view;
        kids_panel = KP_HOME;
        kids_perf_manual = 0;
    }
    if ((down & (1u << B_REC)) && kids_mode != KIDS_STEPS) {
        if (kids_rec_armed || seq.rec) kids_rec_stop();
        else if (playing()) kids_rec_start();
        else kids_rec_armed = 1;
    }
    if (down & (1u << B_PLAY)) {
        kids_perf_manual = 0;
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
        kh_begin(&kh_edit, KH_SMALL, "Octave", sel_trk(), 0);
        kids_notes_off();
        int previous = ui.oct;
        ui.oct = (int8_t)clampi(ui.oct + d, -4, 4);
        for (int track = 0; track < NTRK; track++)
            kids_root[track] = (uint8_t)clampi(kids_root[track] + (ui.oct - previous) * 12, 1, 127);
        kids_remember_chord(sel_trk());
        kh_end(&kh_edit);
    }

    if (ev[EN_PRESET]) kids_category_step(ev[EN_PRESET]);
    if (ev[EN_ALGO]) kids_sound_step(ev[EN_ALGO]);
    if (kids_mode == KIDS_STEPS || kids_mode == KIDS_PERFORMANCE) {
        if (ev[EN_K4]) kids_perf_manual = 1;
        kids_move_cursor(ev[EN_K4]);
        if (down & (1u << B_EDIT)) {
            kh_begin(&kh_edit, KH_STEP, "Step", sel_trk(), 0);
            kids_toggle_step(kids_selected_step(sel_trk()) % 16);
            kh_end(&kh_edit);
            down &= ~(1u << B_EDIT);
        }
    }
    kids_effect_buttons(down);
    kids_turn_knobs(ev);
    if (kids_delay_tempo != proj.tempo_x10) kids_update_delay();
    if (kids_mode == KIDS_PERFORMANCE) kids_update_performance_page();
    for (int i = 0; i < NKEYS; i++) if (kd & (1u << i)) {
        int w = ordinal(WHITE_K, 16, i), b = ordinal(BLACK_K, 11, i);
        if (kids_mode == KIDS_STEPS) {
            if (w >= 0) {
                kh_begin(&kh_edit, KH_STEP, "Step", sel_trk(), 0);
                kids_toggle_step(w);
                kh_end(&kh_edit);
            }
            else if (b >= 0 && is_drum(sel_trk())) ui.dsound = (uint8_t)b;
        } else kids_key_press(i);
    }
    if (kids_mode == KIDS_STEPS || kids_mode == KIDS_PERFORMANCE) kids_note_knobs(ev);
    return 1;
}
static uint32_t kids_button_lights(void)
{
    int track = sel_trk();
    const uint8_t *shape = kids_shaping[track];
    uint32_t buttons = kids_mode == KIDS_HOME ? 1u << B_HOME :
        kids_mode == KIDS_STEPS || kids_mode == KIDS_PERFORMANCE ? 1u << B_SEQ : 0;
    if (shape[KD_ENV_ON]) buttons |= 1u << B_ENV;
    if (shape[KD_LFO_ON]) buttons |= 1u << B_LFO;
    if (kids_mode != KIDS_STEPS && kids_mode != KIDS_PERFORMANCE && shape[KD_EDIT_ON]) buttons |= 1u << B_EDIT;
    if (kids_fx_on[sel_trk()][0]) buttons |= 1u << B_SEL;
    if (kids_fx_on[sel_trk()][1]) buttons |= 1u << B_FX;
    if (kids_arp_on[sel_trk()] && !is_drum(sel_trk())) buttons |= 1u << B_ARP;
    if (playing()) buttons |= 1u << B_PLAY;
    if (seq.rec || kids_rec_armed) buttons |= 1u << B_REC;
    return buttons;
}

static void kids_leds(void)
{
    uint32_t keys = 0, mid = 0;
    if (proj.lights) {
        keys = ui.keys;
        if (kids_mode == KIDS_STEPS) {
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
    loom_lights(kids_button_lights(), keys, proj.lights == 1, mid & ~keys, 0);
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
#define KIDS_BRAND "LION KIDS"
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
    static const uint8_t glyphs[KC_COUNT] = {2, 1, 0, 3, 6, 5, 4, 7};
    switch (glyphs[kind]) {
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
static int kids_label(int x, int y, const loom_font_t *font, const char *s, uint16_t ink, int width)
{
    char out[40];
    int n = 0;
    while (s[n] && n < (int)sizeof out - 1) { out[n] = s[n]; n++; }
    out[n] = 0;
    while (n && text_w(font, out) > width) out[--n] = 0;
    cv_text(x, y, font, out, ink);
    return x + text_w(font, out);
}
static void kids_sound_name(char *name)
{
    memcpy(name, proj.trk[sel_trk()].name, NAME_LEN);
    name[NAME_LEN] = 0;
    if (!name[0]) s_cat(name, is_drum(sel_trk()) ? "Loom" : "Sound");
    if (is_drum(sel_trk()) && name_is("kit", name)) s_cat(name, "Loom");
}
static void kids_preset_label(int x, int y, int width, const loom_font_t *font, const char *name, uint16_t ink)
{
    int position;
    int count = kids_sound_count(&position);
    if (!position) {
        kids_label(x, y, font, name, ink, width);
        return;
    }
    char tally[16];
    num_s((unsigned)count, s_cat(num_s((unsigned)position, tally), "/"));
    int tally_width = text_w(font, tally);
    int end = kids_label(x, y, font, name, ink, width - tally_width - 6);
    cv_text(end + 6, y, font, tally, KIDS_WHITE);
}

static int kids_lit(int t)
{
    if (t == sel_trk() && kids_mode != KIDS_STEPS && ui.keys) return 1;
    return playing() && !(seq.mute & (1u << t)) && step_on(&tpat(t)->st[eng_step[t] % NSTEP], t);
}
static uint16_t kids_pixel_color(int color)
{
    static const uint16_t palette[KPIX_COLORS] = {
        0, RGB(27,29,42), RGB(244,249,255), RGB(255,208,70), RGB(246,86,103),
        RGB(221,153,88), RGB(255,234,109), RGB(254,138,64), RGB(74,204,232),
        RGB(126,190,222), RGB(184,137,243), RGB(96,217,126), RGB(17,155,165),
        RGB(255,155,207), RGB(120,123,228), RGB(119,138,161)
    };
    return color ? palette[color] : KIDS_BG;
}

static int kids_has_pixels(const uint8_t *pixels)
{
    if (pixels) for (int i = 0; i < KPIX_BYTES; i++) if (pixels[i]) return 1;
    return 0;
}

static void kids_draw_pixels(const uint8_t *pixels, int x, int y, int scale)
{
    if (!pixels) return;
    for (int row = 0; row < KPIX_SIDE; row++)
        for (int col = 0; col < KPIX_SIDE; col++) {
            int c = kpix_get(pixels, col, row);
            uint16_t ink = c ? kids_pixel_color(c) : ((row + col) & 1) && scale > 2 ? KIDS_PANEL : KIDS_BG;
            if (c || scale > 2) cv_rect(x + col * scale, y + row * scale, scale, scale, ink);
        }
}

static void kids_panel_title(void)
{
    static const char *const names[] = {"", "Reverb", "Delay", "Arpeggiator", "Songs", "Quick FX"};
    uint16_t color = THEMES[THEME_BW].ink[sel_trk()];
    if (kids_panel == KP_REVERB) {
        for (int i = 0; i < 3; i++) {
            int width = 7 + i * 5;
            cv_rect(15 - width / 2, 4 + i * 4, width, 1, color);
        }
    } else if (kids_panel == KP_DELAY) {
        for (int i = 0; i < 3; i++) cv_rect(6 + i * 6, 5 + i * 2, 5 - i, 9 - i * 2, color);
    } else if (kids_panel == KP_ARP) {
        for (int i = 0; i < 3; i++) cv_rect(6 + i * 6, 13 - i * 4, 5, 3, color);
    } else if (kids_panel == KP_QUICK) {
        cv_line(6, 14, 13, 6, color);
        cv_line(13, 6, 18, 12, color);
        cv_line(18, 12, 24, 4, color);
    } else {
        cv_rect(9, 5, 12, 10, color);
        cv_rect(14, 3, 2, 14, KIDS_BG);
    }
    kids_text(30, 5, names[kids_panel]);
    if (kids_panel == KP_GLOBAL) {
        uint8_t preview[KPIX_BYTES];
        ks_preview_name(ks_selected, preview);
        kids_draw_pixels(ks_selected == ks_loaded ? kpix_song : preview, 68, 3, 2);
        kids_text(88, 5, ks_selected == ks_loaded ? "Loaded" : "Preview");
        kids_text(133, 5, "Hold GLO: clear");
        return;
    }
    if (kids_panel == KP_REVERB || kids_panel == KP_DELAY) kids_text(138, 5, "K2-K4 shared");
    else if (kids_panel == KP_ARP && is_drum(sel_trk())) kids_text(138, 5, "Melodic only");
    else if (kids_panel == KP_ARP) kids_text(172, 5, "BPM sync");
}

static void kids_panel_labels(const tpat_t *pattern)
{
    static const char *const chord_names[] = {"Single", "In scale", "Major", "Minor", "Power", "Min 7", "Octaves", "Maj 7"};
    static const char *const scale_names[] = {"Chromatic", "Major", "Minor", "Maj pent", "Min pent", "Dorian", "Egyptian"};
    static const char *const labels[][4] = {
        {"K1 Volume", "K2 Length", "K3 Chord", "K4 Scale"},
        {"K1 Amount", "K2 Decay", "K3 Room", "K4 Tone"},
        {"K1 Amount", "K2 Repeats", "K3 Time", "K4 Tone"},
        {"K1 Order", "K2 Rate", "K3 Chord", "K4 Scale"},
        {"K1 BPM", "K2 Click", "K3 Swing", "K4 Slot"},
        {"K1 Filter", "K2 Drive", "K3 Crush", "K4 Chorus"}
    };
    int track = sel_trk(), drum = is_drum(track);
    uint16_t ink = THEMES[THEME_BW].ink[track];
    int value[4] = {0};
    const char *word[4] = {NULL};
    char slot_text[12];
    switch (kids_panel) {
    case KP_HOME:
        value[0] = proj.trk[track].par[TP_LEVEL];
        value[1] = pattern->len;
        word[2] = drum ? "-" : chord_names[kids_chord[track]];
        word[3] = drum ? "-" : scale_names[kids_scale[track]];
        break;
    case KP_REVERB: {
        const uint8_t *reverb = proj.trk[AUX_FX2].par;
        value[0] = kids_fx_amount[track][1];
        value[1] = reverb[TP_E2]; value[2] = reverb[TP_E1]; value[3] = reverb[TP_E3];
        break;
    }
    case KP_DELAY: {
        const uint8_t *delay = proj.trk[AUX_FX1].par;
        value[0] = kids_fx_amount[track][0];
        value[1] = delay[TP_E3]; word[2] = kids_delay_names[kids_delay_actual]; value[3] = delay[TP_E4];
        break;
    }
    case KP_ARP:
        word[0] = drum ? "-" : kids_arp_names[kids_arp_order[track]];
        word[1] = drum ? "-" : kids_rate_names[kids_arp_rate[track]];
        word[2] = drum ? "-" : chord_names[kids_chord[track]];
        word[3] = drum ? "-" : scale_names[kids_scale[track]];
        break;
    case KP_QUICK:
        for (int k = 0; k < 4; k++) value[k] = kids_shaping[track][KD_LOWPASS + k];
        break;
    case KP_GLOBAL:
        value[0] = proj.tempo_x10 / 10;
        value[1] = kids_metro;
        if (!kids_metro) word[1] = "Off";
        s_cat(num_s((uint32_t)ks_selected + 1u, slot_text),
            !ks_ready || (ks_bad >> ks_selected & 1u) ? " Check" :
            ks_has_song(ks_slots[ks_selected].blob) ? " Used" : " Empty");
        value[2] = pattern->groove > 0 ? pattern->groove : 0;
        if (!value[2]) word[2] = "Straight";
        word[3] = slot_text;
        break;
    }
    for (int k = 0; k < 4; k++) if (*labels[kids_panel][k]) {
        int x = 6 + k * 60;
        kids_label(x, 3, &LFONT_XS, labels[kids_panel][k], KIDS_WHITE, 56);
        if (kids_panel == KP_QUICK) {
            cv_rect(x, 25, 51, 5, KIDS_PANEL);
            if (value[k]) cv_rect(x, 25, (51 * value[k] + 126) / 127, 5, ink);
        } else if (word[k]) kids_label(x, 20, &LFONT_XS, word[k], ink, 54);
        else kids_number(x, 20, value[k]);
    }
}

static void kids_graph_knobs(int track, uint16_t ink)
{
    const uint8_t *p = kids_shaping[track];
    if (kids_mode == KIDS_ENV) {
        static const char *const labels[] = {"K1 Attack", "K2 Hold", "K3 Sustain", "K4 Release"};
        for (int k = 0; k < 4; k++) {
            int x = 6 + k * 57, parameter = KD_ATTACK + k;
            char value[12];
            kids_label(x, 3, &LFONT_XS, labels[k], KIDS_WHITE, 53);
            if (parameter == KD_SUSTAIN)
                s_cat(num_s((uint32_t)((p[parameter] * 100 + 63) / 127), value), "%");
            else
                s_cat(num_s((uint32_t)kids_envelope_ms(parameter, p[parameter]), value), " ms");
            kids_label(x, 20, &LFONT_XS, value, ink, 54);
        }
    } else {
        kids_text(6, 3, "K1 Depth"); kids_text(66, 3, "K2 Target");
        kids_text(126, 3, "K3 Rate"); kids_text(186, 3, "K4 Wave");
        int depth = p[KD_DEPTH] - KD_DEPTH_ZERO;
        char value[12];
        char *digits = value;
        if (depth < 0) *digits++ = '-';
        s_cat(num_s((uint32_t)(((depth < 0 ? -depth : depth) * 100 + 63) / KD_DEPTH_ZERO), digits), "%");
        kids_label(6, 20, &LFONT_XS, value, ink, 54);
        kids_label(66, 20, &LFONT_XS, kids_lfo_targets[p[KD_TARGET]], ink, 54);
        kids_label(126, 20, &LFONT_XS, kids_lfo_rates[p[KD_RATE]], ink, 54);
        kids_label(186, 20, &LFONT_XS, kids_lfo_waves[p[KD_WAVE]], ink, 54);
    }
}

static void kids_graph(int track, uint16_t ink)
{
    enum { LEFT = 23, WIDTH = 192, MID = 62, GAIN = 42 };
    const uint8_t *p = kids_shaping[track];
    int on = p[kids_mode == KIDS_ENV ? KD_ENV_ON : KD_LFO_ON];
    uint16_t trace = on ? ink : col_mix(KIDS_BG, ink, 7);
    cv_rect(10, 2, 220, 119, KIDS_PANEL);
    cv_rect(12, 4, 216, 115, KIDS_BG);
    for (int i = 0; i <= 4; i++) cv_rect(LEFT + i * WIDTH / 4, 18, 1, 86, KIDS_PANEL);
    for (int i = 0; i < 3; i++) cv_rect(LEFT, MID - GAIN + GAIN * i, WIDTH, 1, KIDS_PANEL);
    if (kids_mode == KIDS_ENV) {
        const int a = kids_envelope_ms(KD_ATTACK, p[KD_ATTACK]);
        const int h = kids_envelope_ms(KD_HOLD, p[KD_HOLD]);
        const int r = kids_envelope_ms(KD_RELEASE, p[KD_RELEASE]);
        const int d = KD_DECAY_MS, sustain_preview = 300, total = a + h + d + sustain_preview + r;
        const int attack = LEFT + WIDTH * a / total, hold = LEFT + WIDTH * (a + h) / total;
        const int decay = LEFT + WIDTH * (a + h + d) / total;
        const int release = LEFT + WIDTH * (a + h + d + sustain_preview) / total;
        const int top = MID - GAIN, base = MID + GAIN, level = base - 2 * GAIN * p[KD_SUSTAIN] / 127;
        cv_line(LEFT, base, attack, top, trace);
        cv_line(attack, top, hold, top, trace);
        cv_line(hold, top, decay, level, trace);
        cv_line(decay, level, release, level, trace);
        cv_line(release, level, LEFT + WIDTH, base, trace);
        cv_rect(hold - 2, top - 2, 5, 5, trace);
        cv_rect(release - 2, level - 2, 5, 5, trace);
    } else {
        int wave = p[KD_WAVE];
        float depth = (float)(GAIN * (p[KD_DEPTH] - KD_DEPTH_ZERO)) * (1.0f / KD_DEPTH_ZERO);
        int previous = MID - (int)(kids_lfo_shape(wave, 0.0f) * depth);
        for (int x = 2; x <= WIDTH; x += 2) {
            int level = MID - (int)(kids_lfo_shape(wave, (float)(x < WIDTH ? x : WIDTH - 1) / WIDTH) * depth);
            cv_line(LEFT + x - 2, previous, LEFT + x, level, trace);
            previous = level;
        }
        if (on && (playing() || ui.keys)) {
            int x = (kids_phase_pixel[track] / 4) * 4;
            int level = MID - (int)(kids_lfo_shape(wave, (float)x / WIDTH) * depth);
            cv_rect(LEFT + x, 18, 1, 86, col_mix(KIDS_BG, ink, 6));
            cv_rect(LEFT + x - 2, level - 2, 5, 5, KIDS_WHITE);
        }
    }
}

static void kids_history_strip(void)
{
    if (kh_memory_full) {
        kids_text(7, 12, "History memory full");
        return;
    }
    cv_rect(117, 5, 1, 30, KIDS_PANEL);
    for (int side = 0; side <= 1; side++) {
        const kh_edit_t *e = kh_next(side);
        int x = 7 + 119 * side;
        kids_text(x, 3, side ? "Redo >" : "K1 < Undo");
        kids_number(x + 94, 3, side ? kh_count - kh_cursor : kh_cursor);
        if (e) {
            uint16_t color = THEMES[THEME_BW].ink[e->track];
            cv_rect(x, 22, 3, 11, color);
            kids_label(x + 7, 19, &LFONT_XS, e->name, color, 100);
        } else kids_text(x, 19, "-");
    }
}

/* Reuse the platform canvas for slices of a display band. */
static int kids_draw_strip(int row, int height, uint16_t bg)
{
    int capacity = CV_MAX / 240;
    if (height > 120) height = 120; /* Match the original viewport's lower edge. */
    if (row >= height) return 0;
    cv_begin(240, (unsigned)(height - row < capacity ? height - row : capacity), bg);
    cv_oy = -row;
    return (int)cv_h;
}

static int kids_shelf_dialog(void)
{
    static uint8_t visible;
    if (!ks_modal && !kpix_open) {
        if (visible) memset(kids_sig, 0, sizeof kids_sig);
        visible = 0;
        return 0;
    }
    int view = kpix_open ? 2 : 1;
    if (visible != view) memset(kids_sig, 0, sizeof kids_sig);
    visible = (uint8_t)view;
    uint16_t ink = THEMES[THEME_BW].ink[sel_trk()];
    int notice = ks_note_active();
    int owner = kpix_open ? ks_name_owner(kpix_slot) : -1;
    int y = 0;
    static const int editor_h[] = {32, 124, 44, 40};
    for (int band = 0; band < 4; band++) {
        int height = kpix_open ? editor_h[band] : 60;
        uint32_t h = hash_u(2166136261u, 128 + ks_modal + 8 * view);
        h = hash_u(h, ks_target);
        if (kpix_open) {
            h = hash_u(h, kpix_generation);
            h = hash_u(h, (unsigned)(owner + 1));
            for (int i = 0; i < KPIX_BYTES; i++) h = hash_u(h, kpix_song[i]);
            h = hash_u(h, (unsigned)(kpix_slot | kpix_x << 4 | kpix_y << 8 | kpix_color << 12));
        }
        if (notice) { h = hash_s(h, ks_message); h = hash_u(h, ks_saved_slot); }
        if (kids_sig[band] == h) { y += height; continue; }
        kids_sig[band] = h;
        kids_draws++;
        for (int row = 0, rows; (rows = kids_draw_strip(row, height, KIDS_BG)); row += rows) {
        if (kpix_open) {
            if (!band) {
                kids_label(9, 3, &LFONT_B, KIDS_BRAND, KIDS_WHITE, 83);
                kids_label(9, 17, &LFONT_XS, "Pixel song names", ink, 110);
                kids_draw_pixels(kpix_song, 114, 7, 2);
                kids_number(204, 9, kpix_slot + 1);
                if (owner >= 0) {
                    char used[16];
                    num_s((unsigned)owner + 1u, s_cat(used, "Used by "));
                    kids_text(132, 9, used);
                }
            } else if (band == 1) {
                enum { PIXEL_CELL = 15, PIXEL_LEFT = 60, PIXEL_TOP = 2 };
                kids_draw_pixels(kpix_bank[kpix_slot], PIXEL_LEFT, PIXEL_TOP, PIXEL_CELL);
                int x = PIXEL_LEFT + PIXEL_CELL * kpix_x, cy = PIXEL_TOP + PIXEL_CELL * kpix_y;
                cv_rect(x, cy, PIXEL_CELL, 1, KIDS_WHITE);
                cv_rect(x, cy + PIXEL_CELL - 1, PIXEL_CELL, 1, KIDS_WHITE);
                cv_rect(x, cy, 1, PIXEL_CELL, KIDS_WHITE);
                cv_rect(x + PIXEL_CELL - 1, cy, 1, PIXEL_CELL, KIDS_WHITE);
            } else if (band == 2) {
                kids_text(8, 1, "K1 Slot"); kids_text(66, 1, "K2 X"); kids_text(124, 1, "K3 Y"); kids_text(181, 1, "K4 Color");
                for (int i = 0; i < KPIX_COLORS; i++) {
                    cv_rect(8 + i * 14, 19, 13, 17, i == kpix_color ? KIDS_WHITE : KIDS_PANEL);
                    cv_rect(10 + i * 14, 21, 9, 13, kids_pixel_color(i));
                }
            } else {
                kids_text(8, 1, "SEL Reset slot"); kids_text(130, 1, "PLAY Paint");
                if (notice) kids_label(8, 19, &LFONT_XS, ks_message, ink, 146);
                else kids_text(8, 19, "EDIT Name song");
                kids_text(155, 19, "HOME Exit");
            }
        } else if (!band) {
            char title[24];
            if (ks_modal != KS_DELETE) num_s(ks_target + 1u, s_cat(title, "Open song "));
            kids_label(12, 5, &LFONT_B, KIDS_BRAND, KIDS_WHITE, 83);
            kids_label(12, 25, &LFONT_B, ks_modal == KS_DELETE ? "Empty slot?" : title, ink, 170);
            uint8_t name[KPIX_BYTES];
            ks_preview_name(ks_target, name);
            kids_draw_pixels(name, 199, 18, 2);
            cv_rect(12, 58, 216, 1, KIDS_PANEL);
        } else if (band == 1) {
            char message[36];
            if (ks_modal == KS_DELETE) {
                char *end = s_cat(message, "Slot ");
                end = num_s(ks_target + 1u, end);
                s_cat(end, ks_has_song(ks_slots[ks_target].blob) || (ks_target == ks_loaded && ui.dirty) ?
                    " will be cleared" : " is already empty");
            }
            kids_text(12, 10, ks_modal == KS_DELETE ? message : "Working song has unsaved edits");
        } else if (band == 2) {
            kids_text(12, 5, ks_modal == KS_DELETE ? "EDIT  Confirm delete" : "EDIT  Discard edits and open");
            if (ks_modal == KS_OPEN_DIRTY && ks_loaded < KS_SLOTS && ks_loaded != ks_target)
                kids_text(12, 29, "SAVE  Save current song and open");
        } else {
            kids_text(12, 6, "HOME  Cancel");
            if (notice) kids_label(12, 29, &LFONT_XS, ks_message, ink, 210);
        }
        cv_blit(0, y + row);
        }
        y += height;
    }
    return 1;
}

static void kids_shelf_notice(uint16_t ink)
{
    char slot[8];
    kids_label(6, 5, &LFONT_B, ks_message, ink, 195);
    if (ks_saved_slot) kids_label(203, 5, &LFONT_B, num_s(ks_saved_slot, slot), ink, 26);
}

static void kids_note_labels(int track, int selected, int velocity, uint16_t ink)
{
    const step_t *step = &tpat(track)->st[selected];
    const int occupied = kids_has_step_note(step, track);
    const int drum = is_drum(track);
    char root[12], length[24];
    int quarters = step_len_q(step->len);
    const char *length_label = occupied ? "Auto" : "-";
    if (occupied && quarters) {
        if (quarters % 4) s_cat(num_s((unsigned)quarters, length), "/4 step");
        else s_cat(num_s((unsigned)quarters / 4, length), " step");
        length_label = length;
    }
    kids_text(6, 3, "K1 Vel.");
    kids_text(63, 3, drum ? "Drum hit" : "K2 Root");
    kids_text(124, 3, "K3 Length");
    kids_text(181, 3, "K4 Step");
    if (velocity) kids_number(6, 20, velocity);
    else kids_text(6, 20, "-");
    kids_label(63, 20, &LFONT_XS, drum ? DL_SOUND_NAME[ui.dsound] :
        (note_name(occupied ? step->note[0] : ui.rem[0], root), root), ink, 58);
    kids_label(124, 20, &LFONT_XS, length_label, ink, 56);
    kids_number(181, 20, selected + 1);
}

static int glo_overlay_panel_draw(void)
{
    if (!glo_overlay_screen) return 0;
    if (plat_ms() - glo_overlay_screen_at >= 7000u) {
        glo_overlay_screen = glo_overlay_painted = 0;
        memset(kids_sig, 0, sizeof kids_sig);
        return 0;
    }
    if (glo_overlay_painted) return 1;
    glo_overlay_painted = 1;
    const uint16_t rose = RGB(246, 104, 122);
    const char *const message[] = {"ME AGAPI - O MPAMPAS", "GIA TO LIONTARAKI MOU"};
    const uint8_t heart[] = {0x36, 0x7f, 0x7f, 0x3e, 0x1c, 0x08};
    lcd_fill(0, 0, 240, 240, 0);
    for (int row = 0, rows; (rows = kids_draw_strip(row, 83, 0)); row += rows) {
    for (int i = 0; i < 2; i++) {
        const loom_font_t *font = &LFONT_M;
        if (text_w(font, message[i]) > 220) font = &LFONT_B;
        cv_text((240 - text_w(font, message[i])) / 2, 3 + 22 * i, font, message[i], rose);
    }
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 7; x++)
            if ((heart[y] >> x) & 1u) cv_rect(106 + 4 * x, 52 + 4 * y, 4, 4, rose);
    cv_blit(0, 80 + row);
    }
    return 1;
}

static void kids_draw(void)
{
    if (glo_overlay_panel_draw() || kids_shelf_dialog()) return;
    static const uint8_t stage_h[] = {28, 64, 64, 20, 24, 40};
    static const uint8_t steps_h[] = {28, 24, 29, 29, 29, 29, 32, 40};
    static const uint8_t graph_h[] = {28, 28, 128, 16, 40};
    static const uint8_t perf_h[] = {28, 24, 36, 36, 36, 36, 4, 40};
    int performance = kids_mode == KIDS_PERFORMANCE;
    const uint8_t *heights = performance ? perf_h : kids_mode == KIDS_HOME ? stage_h :
        kids_mode == KIDS_STEPS ? steps_h : graph_h;
    int graph = kids_mode == KIDS_ENV || kids_mode == KIDS_LFO;
    int bands = performance || kids_mode == KIDS_STEPS ? 8 : graph ? 5 : 6, trk = sel_trk(), y = 0;
    int now = playing() ? eng_step[trk] : -1;
    uint16_t ink = THEMES[THEME_BW].ink[trk];
    char name[NAME_LEN + 1]; kids_sound_name(name);
    const tpat_t *tp = tpat(trk);
    int rec_status = seq.rec ? 2 : kids_rec_armed ? 1 : 0;
    int shelf = kids_mode == KIDS_HOME && kids_panel == KP_GLOBAL;
    int shelf_notice = ks_note_active();
    int notice_band = shelf ? 4 : bands - 1;
    int full = (int32_t)(kids_full_until - plat_ms()) > 0;
    int focused = (performance || kids_mode == KIDS_STEPS) ? kids_selected_step(trk) : -1;
    int velocity = focused >= 0 ? kids_step_velocity() : 0;
    for (int b = 0; b < bands; b++) {
        uint32_t h = hash_u(2166136261u, kids_mode | trk << 4);
        if (b == 0) {
            h = hash_u(h, rec_status); h = hash_u(h, full);
            h = hash_u(h, now + 1); h = hash_u(h, cued_pat(trk));
            h = hash_u(h, ks_loaded);
            for (int i = 0; i < KPIX_BYTES; i++) h = hash_u(h, kpix_song[i]);
        } else if (kids_mode == KIDS_HOME && (b == 1 || b == 2)) {
            h = hash_u(h, seq.mute);
            for (int t = (b - 1) * 4; t < b * 4; t++) {
                h = hash_u(h, kids_lit(t));
                h = hash_u(h, kids_categories[t]);
            }
        } else if (performance && b >= 2 && b <= 5) {
            for (int t = (b - 2) * 2; t < (b - 2) * 2 + 2; t++) {
                h = hash_u(h, kids_perf_page[t]);
                if (t == trk) h = hash_u(h, (uint32_t)(focused + 1));
                h = hash_u(h, playing() ? eng_step[t] + 1 : 0);
                h = hash_u(h, (seq.mute >> t) & 1u);
                for (int col = 0; col < 16; col++) h = hash_u(h, kids_performance_cell(t, col));
            }
        } else if (kids_mode == KIDS_STEPS && b >= 2 && b <= 5) {
            h = hash_u(h, (uint32_t)(focused + 1));
            h = hash_u(h, now + 1); h = hash_u(h, ui.bar); h = hash_u(h, tp->len);
            for (int i = 0; i < 4; i++) {
                int st = ui.bar * 16 + (b - 2) * 4 + i;
                h = hash_u(h, is_drum(trk) ? kit_has(&tp->st[st], ui.dsound + 1) : step_on(&tp->st[st], trk));
            }
        } else {
            h = hash_s(h, name);
            if (kids_sound_error) h = hash_s(h, kids_sound_error);
            h = hash_u(h, kids_category);
            h = hash_u(h, kids_picks[trk][kids_category]);
            if (graph || (kids_mode == KIDS_HOME && kids_panel == KP_QUICK))
                for (int p = 0; p < KD_COUNT; p++) h = hash_u(h, kids_shaping[trk][p]);
            if (kids_mode == KIDS_LFO && b == 2 && kids_shaping[trk][KD_LFO_ON] && (playing() || ui.keys))
                h = hash_u(h, kids_phase_pixel[trk] / 4 + 1);
            if (kids_mode == KIDS_HOME) {
                h = hash_u(h, kids_panel);
                h = hash_u(h, kids_metro); h = hash_u(h, kids_delay_actual);
                h = hash_u(h, kids_arp_order[trk]); h = hash_u(h, kids_arp_rate[trk]);
                for (int slot = 0; slot < 2; slot++) {
                    h = hash_u(h, kids_fx_amount[trk][slot]);
                    for (int k = TP_E1; k <= TP_E4; k++) h = hash_u(h, proj.trk[AUX_FX1 + slot].par[k]);
                }
            }
            h = hash_u(h, proj.trk[trk].par[TP_LEVEL]); h = hash_u(h, tp->len);
            h = hash_u(h, kids_chord[trk]); h = hash_u(h, kids_scale[trk]);
            h = hash_u(h, kids_perf_page[trk]); h = hash_u(h, (uint8_t)tp->groove);
            h = hash_u(h, velocity); h = hash_u(h, (uint32_t)(focused + 1));
            if (focused >= 0) {
                h = hash_u(h, tp->st[focused].note[0]);
                h = hash_u(h, tp->st[focused].len);
            }
            h = hash_u(h, ui.bar); h = hash_u(h, (uint8_t)ui.oct); h = hash_u(h, ui.dsound); h = hash_u(h, ui.rem[0]);
            h = hash_u(h, proj.tempo_x10); h = hash_u(h, cued_pat(trk));
        }
        if (shelf && b >= 4) {
            h = hash_u(h, (uint32_t)(ks_selected | ks_loaded << 2 | ks_ready << 5 | ks_bad << 6));
            for (int i = 0; i < KS_SLOTS; i++) h = hash_u(h, ks_slots[i].revision);
            if (ks_selected == ks_loaded)
                for (int i = 0; i < KPIX_BYTES; i++) h = hash_u(h, kpix_song[i]);
        }
        if (shelf_notice && b == notice_band) {
            h = hash_s(h, ks_message);
            h = hash_u(h, ks_saved_slot);
        }
        if (b == bands - 1 && kh_visible) {
            h = hash_u(h, 1 + kh_cursor + 16 * kh_count + 256 * kh_memory_full);
            for (int i = 0; i <= 1; i++) {
                const kh_edit_t *e = kh_next(i);
                if (e) { h = hash_s(h, e->name); h = hash_u(h, e->track); }
            }
        }
        if (kids_sig[b] == h) { y += heights[b]; continue; }
        kids_sig[b] = h; kids_draws++;
        for (int row = 0, rows; (rows = kids_draw_strip(row, heights[b], KIDS_BG)); row += rows) {
        if (b == 0) {
            kids_label(6, 5, &LFONT_B, KIDS_BRAND, KIDS_WHITE, 76);
            if (kids_mode != KIDS_HOME)
                kids_label(77, 7, &LFONT_XS, performance ? "Perform" : kids_mode == KIDS_STEPS ? "Steps" :
                    kids_mode == KIDS_ENV ? "Env" : "LFO", ink, 39);
            if (kids_has_pixels(kpix_song)) kids_draw_pixels(kpix_song, 116, 5, 2);
            else kids_number(118, 7, ks_loaded + 1);
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
            if (kh_visible) kids_history_strip();
            else if (shelf_notice && b == notice_band) kids_shelf_notice(ink);
            else if (kids_mode == KIDS_HOME) {
                kids_panel_labels(tp);
            } else if (graph) {
                kids_graph_knobs(trk, ink);
            } else {
                kids_note_labels(trk, focused, velocity, ink);
            }
        } else if (performance) {
            if (b == 1) {
                char page[24];
                num_s((uint32_t)kids_page_count(trk), s_cat(num_s((uint32_t)kids_perf_page[trk] + 1, s_cat(page, "Page ")), "/"));
                kids_text(6, 4, page);
                kids_preset_label(76, 4, 101, &LFONT_XS, name, ink);
                kids_text(180, 4, "EDIT Toggle");
            } else if (b >= 2 && b <= 5) {
                for (int row = 0; row < 2; row++) {
                    int t = (b - 2) * 2 + row, line = row * 18;
                    int playhead = playing() ? eng_step[t] : -1;
                    uint16_t color = THEMES[THEME_BW].ink[t];
                    if (seq.mute & (1u << t)) color = col_mix(KIDS_PANEL, color, 7);
                    if (t == trk) cv_rect(5, line + 3, 2, 12, color);
                    kids_number(10, line + 2, t + 1);
                    kids_number(20, line + 2, kids_perf_page[t] + 1);
                    for (int col = 0; col < 16; col++) {
                        int x = 30 + col * 13, cell = kids_performance_cell(t, col);
                        int focused_cell = t == trk && focused == kids_perf_page[t] * 16 + col;
                        if (focused_cell) cv_rect(x, line + 16, 10, 1, KIDS_WHITE);
                        if (!cell) {
                            cv_rect(x + 3, line + 8, 4, 1, KIDS_PANEL);
                            continue;
                        }
                        int active = playhead == kids_perf_page[t] * 16 + col;
                        uint16_t fill = cell == 2 ? color : KIDS_PANEL;
                        cv_rect(x, line + 3, 10, 12, active ? KIDS_WHITE : fill);
                        if (active) cv_rect(x + 2, line + 5, 6, 8, fill);
                    }
                }
            }
        } else if (kids_mode == KIDS_HOME) {
            if (b == 1 || b == 2) for (int i = 0; i < 4; i++) {
                int t = (b - 1) * 4 + i, x = 4 + 59 * i, lit = kids_lit(t);
                uint16_t c = THEMES[THEME_BW].ink[t];
                cv_rect(x, 4, 55, 56, t == trk ? c : KIDS_PANEL);
                cv_rect(x + 2, 6, 51, 52, col_mix(KIDS_BG, c, t == trk ? 4 : 1));
                cv_rect(x + 2, 6, 51, t < 4 ? 3 : 1, c);
                kids_number(x + 5, 10, t + 1);
                kids_icon(kids_categories[t], x + 16, 20 - (lit ? 2 : 0), 27, c);
                if (lit) cv_rect(x + 15, 51, 27, 3, c);
                if (seq.mute & (1u << t)) { cv_line(x + 15, 47, x + 43, 20, c); }
            } else if (b == 3) {
                cv_rect(6, 3, 3, 14, ink);
                kids_number(14, 2, trk + 1);
                if (kids_sound_error) kids_label(29, 1, &LFONT_B, kids_sound_error, ink, 204);
                else kids_preset_label(29, 1, 204, &LFONT_B, name, ink);
            } else if (shelf_notice && b == notice_band) kids_shelf_notice(ink);
            else if (kids_panel != KP_HOME) kids_panel_title();
            else {
                char octave[4] = {ui.oct < 0 ? '-' : '+', (char)('0' + (ui.oct < 0 ? -ui.oct : ui.oct)), 0};
                kids_label(6, 5, &LFONT_XS, KIDS_CATEGORY[kids_category], ink, 82);
                kids_text(87, 5, "REC overdub");
                kids_text(180, 5, "OCT");
                kids_text(208, 5, is_drum(trk) ? "-" : octave);
            }
        } else if (kids_mode == KIDS_STEPS) {
            if (b == 1) {
                kids_icon(kids_category, 6, 2, 20, ink);
                kids_preset_label(31, 4, 131, &LFONT_XS, name, ink);
                kids_label(170, 4, &LFONT_XS, is_drum(trk) ? DL_SOUND_NAME[ui.dsound] : "WHITE keys", KIDS_WHITE, 65);
            } else if (b <= 5) for (int i = 0; i < 4; i++) {
                int st = ui.bar * 16 + (b - 2) * 4 + i, x = 5 + 59 * i;
                int on = st < tp->len && (is_drum(trk) ? kit_has(&tp->st[st], ui.dsound + 1) : step_on(&tp->st[st], trk));
                cv_rect(x, 2, 53, 24, st == now ? KIDS_WHITE : on ? ink : KIDS_PANEL);
                if (st == now) cv_rect(x + 2, 4, 49, 20, on ? ink : KIDS_BG);
                char value[12]; num_s((uint32_t)st + 1, value);
                cv_text(x + 7, 5, &LFONT_B, value, on ? KIDS_BG : st < tp->len ? KIDS_WHITE : RGB(97, 110, 130));
                if (on) cv_rect(x + 39, 11, 5, 5, KIDS_BG);
                if (st == focused) cv_rect(x + 6, 22, 40, 2, on ? KIDS_BG : ink);
            } else {
                for (int i = 0; i < kids_page_count(trk); i++) {
                    cv_rect(6 + i * 43, 5, 38, 22, ui.bar == i ? ink : KIDS_PANEL);
                    char digit[4]; num_s((uint32_t)i + 1, digit);
                    cv_text(21 + i * 43, 8, &LFONT_XS, digit, ui.bar == i ? KIDS_BG : KIDS_WHITE);
                }
                kids_text(180, 10, "EDIT Toggle");
            }
        } else if (b == 1) {
            kids_icon(kids_category, 6, 1, 21, ink);
            kids_number(32, 5, trk + 1);
            if (kids_sound_error) kids_label(47, 3, &LFONT_B, kids_sound_error, ink, 127);
            else kids_preset_label(47, 3, 127, &LFONT_B, name, ink);
            kids_text(182, 5, "Play keys");
        } else if (b == 2) {
            kids_graph(trk, ink);
        } else {
            kids_text(10, 0, kids_mode == KIDS_ENV ?
                (is_drum(trk) ? "Kit hit envelope" : "Note envelope") : "Synced to song tempo");
        }
        cv_blit(0, y + row);
        }
        y += heights[b];
    }
}
#ifdef LION_KIDS_DEVICE
#undef malloc
#undef calloc
#undef free
#endif
#endif
