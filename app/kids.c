/* SPDX-License-Identifier: MPL-2.0 */
/* Emulator-only vertical slice. Included by ui.c after the original input helpers.
 * Musical state stays in proj/pats/seq; only presentation state lives here. */
#if defined(PLAT_HOST) || defined(PLAT_WEB)
static uint8_t kids_mode;                       /* 0 Studio, 1 Stage, 2 Steps, 3 Sounds */
static uint32_t kids_sig[8], kids_draws;
/* Session-only adult credential, deliberately outside project/preset serialization.
 * A simulated reboot keeps the lock; ending the host/browser process ends this session. */
enum { KG_NONE, KG_SET, KG_CONFIRM, KG_UNLOCK };
static uint8_t kids_pin[4], kids_digits[4], kids_pin_set, kids_locked;
static uint8_t kids_gate, kids_entered, kids_error;
static uint32_t kids_retry_at, kids_gate_sig;
static void kids_category_sync(void);

static void kids_gate_open(int gate)
{
    kids_gate = (uint8_t)gate;
    kids_entered = kids_error = 0;
    memset(kids_digits, 0, sizeof kids_digits);
    kids_gate_sig = 0;
    ui_redraw();
}
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
    if (!mode && kids_locked) {                 /* every exit goes through the adult gate */
        kids_gate_open(KG_UNLOCK);
        return;
    }
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
        if (kids_pin_set) {
            kids_locked = 1;
            kids_detach();
        } else kids_gate_open(KG_SET);
        kids_category_sync();
    }
    ui_redraw();
}
static void kids_gate_input(uint32_t down, const int32_t ev[NE])
{
    if (down & (1u << B_HOME)) {
        int setup = kids_gate != KG_UNLOCK;
        kids_gate = KG_NONE;
        kids_gate_sig = 0;
        if (setup) { memset(kids_pin, 0, sizeof kids_pin); kids_set(0); }
        ui_redraw();
        return;
    }
    if ((int32_t)(kids_retry_at - plat_ms()) > 0) return;
    for (int i = 0; i < 4; i++)
        if (ev[EN_K1 + i]) {
            kids_digits[i] = (uint8_t)clampi(kids_digits[i] + ev[EN_K1 + i], 0, 9);
            kids_entered |= (uint8_t)(1u << i);
            kids_error = 0;
        }
    if (!(down & (1u << B_SEL))) return;
    if (kids_entered != 15) { kids_error = 1; return; } /* no default PIN or accidental acceptance */
    if (kids_gate == KG_SET) {
        memcpy(kids_pin, kids_digits, sizeof kids_pin);
        kids_gate_open(KG_CONFIRM);
        return;
    }
    if (memcmp(kids_pin, kids_digits, sizeof kids_pin)) {
        int gate = kids_gate == KG_CONFIRM ? KG_SET : KG_UNLOCK;
        kids_gate_open(gate);
        kids_error = 2;
        kids_retry_at = plat_ms() + 3000u;
        return;
    }
    if (kids_gate == KG_CONFIRM) {
        kids_pin_set = kids_locked = 1;
        kids_detach();
        kids_gate = KG_NONE;
    } else {
        kids_gate = KG_NONE;
        kids_locked = 0;
        kids_set(0);
        say("STUDIO", "SAVE to keep edits");
    }
    memset(kids_digits, 0, sizeof kids_digits);
    ui_redraw();
}
/* Sound browsing is presentation-only: resolve the current sound from the model,
 * then call the existing preset/kit helpers (including their undo and engine update). */
static uint8_t kids_category = 6;
static const char *const KIDS_CATEGORY[] = {"BASS", "KEYS", "PADS", "LEADS", "PLUCKS", "TEXTURES", "ALL"};
static int kids_role(int role)
{
    switch (role) {
    case RL_BASS: return 0;
    case RL_KEYS: case RL_CHORD: case RL_STAB: return 1;
    case RL_PAD: case RL_STRINGS: case RL_VOICE: return 2;
    case RL_LEAD: case RL_BRASS: return 3;
    case RL_PLUCK: case RL_BELL: return 4;
    default: return 5;
    }
}
static void kids_notes_off(void)
{
    engine_keys_off(inst_trk());
    memset(ui.kmode, 0, sizeof ui.kmode);
}
static void kids_category_sync(void)
{
    kids_category = 6;
    if (is_drum(sel_trk())) return;
    for (int i = 0; i < NPRESETS; i++)
        if (preset_engine(i) == proj.trk[sel_trk()].par[TP_ENGINE] && name_is(PRESETS[i].name, proj.trk[sel_trk()].name)) {
            kids_category = (uint8_t)kids_role(PRESETS[i].role);
            return;
        }
}
static int kids_sound_match(int i)
{
    int e = preset_engine(i);
    return e >= 0 && e != EG_DRUMLINE && e != EG_MIDI &&
        (kids_category == 6 || (i < NPRESETS && kids_role(PRESETS[i].role) == kids_category));
}
static int kids_sound_current(int i)
{
    if (is_drum(sel_trk()))
        return (i == 0 && name_is("kit", proj.trk[sel_trk()].name)) ||
            name_is(i < NFKITS ? kit_name(i) : user_kit_name[i - NFKITS], proj.trk[sel_trk()].name);
    return preset_engine(i) == proj.trk[sel_trk()].par[TP_ENGINE] &&
        name_is(i < NPRESETS ? PRESETS[i].name : user_presets[i - NPRESETS].name, proj.trk[sel_trk()].name);
}
static int kids_sound_valid(int i)
{
    return is_drum(sel_trk()) ? i < NFKITS || user_kit_name[i - NFKITS][0] : kids_sound_match(i);
}
static int kids_sound_count(int *position)
{
    int n = 0, total = is_drum(sel_trk()) ? NKITS : presets_all();
    *position = 0;
    for (int i = 0; i < total; i++) if (kids_sound_valid(i)) {
        if (kids_sound_current(i)) *position = n + 1;
        n++;
    }
    return n;
}
static void kids_sound_step(int d)
{
    int at, n = kids_sound_count(&at), total = is_drum(sel_trk()) ? NKITS : presets_all();
    if (!n) return;
    int target = at ? ((at - 1 + d) % n + n) % n : d < 0 ? n - 1 : 0;
    for (int i = 0, j = 0; i < total; i++) if (kids_sound_valid(i) && j++ == target) {
        kids_notes_off();
        if (is_drum(sel_trk())) act_kit(i);
        else preset_apply(sel_trk(), i);
        return;
    }
}
static void kids_track(int d)
{
    kids_notes_off();
    track_select(clampi(sel_trk() + d, 0, NTRK - 1));
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
        if ((ku & (1u << i)) && ui.kmode[i] == KM_PLAY) key_up(i);
    ui.btn = btn; ui.keys = keys; ui.btn_used = btn;
    if (kids_gate) { kids_gate_input(down, ev); return 1; }
    if (down & (1u << B_SEL)) {
        kids_notes_off();
        kids_set(0);
        return 1;
    }
    if (down & ((1u << B_HOME) | (1u << B_SEQ) | (1u << B_EDIT))) {
        kids_notes_off();
        kids_mode = down & (1u << B_HOME) ? 1 : down & (1u << B_SEQ) ? 2 : 3;
        if (kids_mode == 3) kids_category_sync();
    }
    if (down & (1u << B_PLAY)) {
        uint32_t held = ui.btn;
        ui.btn = 1u << B_PLAY;
        play_press();
        ui.btn = held;
    }
    int d = (int)(ev[EN_ALGO] + ev[EN_K1]);
    if (d) kids_track(d);
    d = !!(down & (1u << B_OCTUP)) - !!(down & (1u << B_OCTDN));
    if (kids_mode == 2) d += (int)ev[EN_K2];
    ui.bar = (uint8_t)clampi(ui.bar + d, 0, 3);
    if (ev[EN_SELECT]) steps_knob(0, ev[EN_SELECT]); /* SELECT always selects a pattern */
    if (kids_mode == 1 && ev[EN_K2]) steps_knob(0, ev[EN_K2]);
    if (kids_mode == 3 && ev[EN_K2] && !is_drum(sel_trk())) {
        kids_category = (uint8_t)((kids_category + ev[EN_K2] % 7 + 7) % 7);
        kids_sound_step(1);
    }
    if (ev[EN_K3]) {
        if (kids_mode != 2) kids_sound_step(ev[EN_K3]);
        else if (is_drum(sel_trk())) ui.dsound = (uint8_t)clampi(ui.dsound + ev[EN_K3], 0, DL_NSOUND - 1);
        else { ui.rem[0] = (uint8_t)clampi(ui.rem[0] + ev[EN_K3], 1, 127); ui.nrem = 1; }
    }
    if (ev[EN_K4]) set_tempo_x10(proj.tempo_x10 + ev[EN_K4] * 10);
    if (ev[EN_PRESET]) kids_sound_step(ev[EN_PRESET]); /* PRESETS always means sound */
    for (int i = 0; i < NKEYS; i++) if (kd & (1u << i)) {
        int w = ordinal(WHITE_K, 16, i), b = ordinal(BLACK_K, 11, i);
        if (kids_mode == 1) {
            if (w >= 0 && w < NTRK) kids_track(w - sel_trk());
        } else if (kids_mode == 2) {
            if (w >= 0) step_toggle(w);
            else if (b >= 0 && is_drum(sel_trk())) ui.dsound = (uint8_t)b;
        } else {
            int note = is_drum(sel_trk()) ? key_sound(i) : key_note(i);
            ui.kmode[i] = KM_PLAY; ui.knote[i] = (uint8_t)note;
            engine_key(sel_trk(), note, 1, 100);
            if (is_drum(sel_trk())) ui.dsound = (uint8_t)(note - 1);
            else { ui.rem[0] = (uint8_t)note; ui.nrem = 1; }
        }
    }
    return 1;
}
static void kids_leds(void)
{
    uint32_t buttons = 1u << (kids_gate ? B_SEL : kids_mode == 1 ? B_HOME : kids_mode == 2 ? B_SEQ : B_EDIT);
    uint32_t keys = 0, mid = 0;
    if (playing()) buttons |= 1u << B_PLAY;
    if (!kids_gate && proj.lights) {
        keys = ui.keys;
        if (kids_mode == 1) {
            keys |= 1u << WHITE_K[sel_trk()];
            for (int i = 0; i < NTRK; i++) mid |= 1u << WHITE_K[i];
        } else if (kids_mode == 2) {
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
static void kids_gate_draw(void)
{
    uint32_t h = hash_u(2166136261u, kids_gate);
    int waiting = (int32_t)(kids_retry_at - plat_ms()) > 0;
    h = hash_u(h, kids_entered | kids_error << 8 | waiting << 16);
    for (int i = 0; i < 4; i++) h = hash_u(h, kids_digits[i]);
    if (h == kids_gate_sig) return;
    kids_gate_sig = h;
    const char *lines[8] = {
        kids_gate == KG_UNLOCK ? "ADULT UNLOCK" : "ADULT SETUP",
        kids_gate == KG_SET ? "Choose a private 4-digit PIN" :
        kids_gate == KG_CONFIRM ? "Enter the same PIN again" : "Enter your session PIN",
        "K1 / K2 / K3 / K4: one digit each", "",
        waiting ? "Incorrect PIN - wait 3 seconds" : kids_error == 1 ? "Set all four digits first" :
        kids_gate == KG_UNLOCK ? "SEL Unlock Studio" : kids_gate == KG_SET ? "SEL Confirm PIN" : "SEL Start Kids",
        "HOME Cancel",
        "Saved projects stay untouched",
        "Studio: SAVE to keep this copy"
    };
    for (int b = 0; b < 8; b++) {
        cv_begin(240, 30, RGB(0, 0, 0));
        if (b == 3) {
            for (int i = 0; i < 4; i++) {
                if (kids_entered & (1u << i)) kids_number(15 + 59 * i, 6, kids_digits[i]);
                else kids_text(15 + 59 * i, 6, "-");
            }
        } else kids_text(6, 6, lines[b]);
        cv_blit(0, b * 30);
        kids_draws++;
    }
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
static void kids_lock(int x, int y, uint16_t c)
{
    cv_rect(x + 2, y, 6, 2, c); cv_rect(x + 1, y + 2, 2, 4, c);
    cv_rect(x + 7, y + 2, 2, 4, c); cv_rect(x, y + 6, 10, 7, c);
    cv_rect(x + 4, y + 8, 2, 3, KIDS_BG);
}
static void kids_grid(int x, int y, uint16_t c)
{
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) cv_rect(x + a * 7, y + b * 7, 5, 5, c);
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
    return playing() && !(seq.mute & (1u << t)) && step_on(&tpat(t)->st[eng_step[t] % NSTEP], t);
}
static void kids_draw(void)
{
    if (kids_gate) { kids_gate_draw(); return; }
    static const uint8_t stage_h[] = {28, 64, 64, 20, 24, 40};
    static const uint8_t steps_h[] = {28, 24, 29, 29, 29, 29, 32, 40};
    static const uint8_t sound_h[] = {28, 32, 64, 38, 38, 40};
    const uint8_t *heights = kids_mode == 1 ? stage_h : kids_mode == 2 ? steps_h : sound_h;
    int bands = kids_mode == 2 ? 8 : 6, trk = sel_trk(), y = 0;
    int now = playing() ? eng_step[trk] : -1, pos, count = kids_sound_count(&pos);
    uint16_t ink = THEMES[THEME_BW].ink[trk];
    char name[NAME_LEN + 1]; kids_sound_name(name);
    const tpat_t *tp = tpat(trk);
    for (int b = 0; b < bands; b++) {
        uint32_t h = hash_u(2166136261u, kids_mode | trk << 4);
        if (b == 0) {
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
            h = hash_s(h, name); h = hash_u(h, kids_category); h = hash_u(h, pos);
            h = hash_u(h, ui.bar); h = hash_u(h, ui.dsound); h = hash_u(h, ui.rem[0]);
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
            for (int i = 0; i < 4; i++) cv_rect(185 + i * 7, 11, 4, 4, now >= 0 && now % 4 == i ? ink : KIDS_PANEL);
            kids_lock(222, 7, KIDS_WHITE);
            cv_rect(6, 27, 228, 1, KIDS_PANEL);
        } else if (b == bands - 1) {
            const char *labels[4] = {"1 Track", kids_mode == 1 ? "2 Pattern" : kids_mode == 2 ? "2 Bar" : is_drum(trk) ? "2 -" : "2 Group",
                kids_mode == 2 ? (is_drum(trk) ? "3 Hit" : "3 Note") : "3 Sound", "4 Tempo"};
            int v[4] = {trk + 1, kids_mode == 1 ? cued_pat(trk) + 1 : kids_mode == 2 ? ui.bar + 1 : kids_category + 1,
                kids_mode == 2 ? (is_drum(trk) ? ui.dsound + 1 : ui.rem[0]) : pos, proj.tempo_x10 / 10};
            for (int k = 0; k < 4; k++) {
                char value[12]; num_s((uint32_t)v[k], value);
                cv_rect(k * 60 + 3, 0, 54, 1, k == 0 ? ink : KIDS_PANEL);
                kids_label(k * 60 + 5, 3, &LFONT_XS, labels[k], KIDS_WHITE, 53);
                kids_label(k * 60 + 5, 19, &LFONT_B, kids_mode == 3 && k == 1 && is_drum(trk) ? "-" : value, ink, 53);
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
                kids_label(29, 1, &LFONT_B, name, ink, 204);
            } else {
                kids_grid(8, 5, ink); kids_text(26, 5, "SEQ");
                kids_icon(7, 84, 4, 16, ink); kids_text(104, 5, "EDIT");
                kids_lock(177, 4, KIDS_WHITE); kids_text(193, 5, "SEL");
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
                kids_icon(is_drum(trk) ? 0 : kids_category == 0 ? 1 : kids_category == 1 ? 2 : kids_category == 2 ? 3 :
                    kids_category == 3 ? 6 : kids_category == 4 ? 4 : 5, 94, ui.keys ? 3 : 7, 52, ink);
                kids_text(20, 27, "<"); kids_text(214, 27, ">");
            } else if (b == 3) {
                cv_rect(6, 1, 228, 32, KIDS_PANEL);
                kids_label(15, 8, &LFONT_B, name, KIDS_WHITE, 211);
            } else {
                for (int i = 0; i < 16; i++) {
                    int black = ordinal(BLACK_K, 11, WHITE_K[i] + 1);
                    cv_rect(8 + i * 14, 0, 12, 20, (ui.keys & (1u << WHITE_K[i])) ? ink : KIDS_WHITE);
                    if (black >= 0) cv_rect(17 + i * 14, 0, 6, 11,
                        (ui.keys & (1u << BLACK_K[black])) ? ink : KIDS_BG);
                }
                kids_text(7, 22, "PRESETS sound"); kids_text(140, 22, "KEYS play");
            }
        }
        cv_blit(0, y); y += heights[b];
    }
}
#endif
