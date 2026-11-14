/* SPDX-License-Identifier: MPL-2.0 */
/* Emulator-only vertical slice. Included by ui.c after the original input helpers.
 * Musical state stays in proj/pats/seq; only presentation state lives here. */
#if defined(PLAT_HOST) || defined(PLAT_WEB)
static uint8_t kids_mode;                       /* 0 Studio, 1 Stage, 2 Steps */
static uint32_t kids_sig[8], kids_draws;
/* Session-only adult credential, deliberately outside project/preset serialization.
 * A simulated reboot keeps the lock; ending the host/browser process ends this session. */
enum { KG_NONE, KG_SET, KG_CONFIRM, KG_UNLOCK };
static uint8_t kids_pin[4], kids_digits[4], kids_pin_set, kids_locked;
static uint8_t kids_gate, kids_entered, kids_error;
static uint32_t kids_retry_at, kids_gate_sig;

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
static int kids_input(void)
{
    if (!kids_mode)
        return 0;
    uint32_t btn = plat_buttons(), keys = plat_keys();
    uint32_t down = btn & ~ui.btn, kd = keys & ~ui.keys;
    int32_t ev[NE];
    for (int i = 0; i < NE; i++)
        ev[i] = plat_enc(i);
    ui.btn = btn;
    ui.keys = keys;
    ui.btn_used = btn;                          /* releases cannot leak into Studio */
    if (kids_gate) {
        kids_gate_input(down, ev);
        return 1;
    }
    if (down & (1u << B_SEL)) {
        kids_set(0);                            /* visible adult unlock, no held gesture */
        return 1;
    }
    if (down & (1u << B_HOME)) kids_mode = 1;
    if (down & (1u << B_SEQ)) kids_mode = 2;
    if (down & (1u << B_PLAY)) {
        uint32_t held = ui.btn;
        ui.btn = 1u << B_PLAY;                  /* Studio-only REC/SEQ modifiers stay inactive */
        play_press();
        ui.btn = held;
    }
    int d = (int)(ev[EN_ALGO] + ev[EN_K1]);
    if (d) track_select(clampi(sel_trk() + d, 0, NTRK - 1));
    d = (int)ev[EN_K2] + !!(down & (1u << B_OCTUP)) - !!(down & (1u << B_OCTDN));
    ui.bar = (uint8_t)clampi(ui.bar + d, 0, 3);
    if (ev[EN_K3]) {
        if (is_drum(sel_trk()))
            ui.dsound = (uint8_t)clampi(ui.dsound + ev[EN_K3], 0, DL_NSOUND - 1);
        else {
            ui.rem[0] = (uint8_t)clampi(ui.rem[0] + ev[EN_K3], 1, 127);
            ui.nrem = 1;
        }
    }
    if (ev[EN_K4]) set_tempo_x10(proj.tempo_x10 + ev[EN_K4] * 10);
    if (ev[EN_PRESET]) steps_knob(0, ev[EN_PRESET]);
    if (kids_mode == 2)
        for (int i = 0; i < NKEYS; i++)
            if (kd & (1u << i)) {
                int w = ordinal(WHITE_K, 16, i), b = ordinal(BLACK_K, 11, i);
                if (w >= 0) step_toggle(w);
                else if (b >= 0 && is_drum(sel_trk())) ui.dsound = (uint8_t)b;
            }
    return 1;
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
static void kids_draw(void)
{
    if (kids_gate) { kids_gate_draw(); return; }
    static const uint8_t height[8] = {24, 26, 26, 32, 32, 32, 32, 36};
    const uint16_t bg = RGB(0, 0, 0), fg = RGB(235, 235, 238);
    int trk = sel_trk(), now = playing() ? eng_step[trk] : -1, y = 0;
    const tpat_t *tp = tpat(trk);
    for (int band_ix = 0; band_ix < 8; band_ix++) {
        uint32_t h = hash_u(2166136261u, kids_mode);
        h = hash_u(h, trk);
        if (band_ix == 0) {
            h = hash_u(h, playing()); h = hash_u(h, recording());
        } else if (band_ix == 1 || band_ix == 2) h = hash_u(h, seq.mute);
        else if ((band_ix == 3 || band_ix == 4) && kids_mode == 2) {
            h = hash_u(h, ui.dsound); h = hash_u(h, ui.bar); h = hash_u(h, now + 1); h = hash_u(h, tp->len);
            for (int i = 0; i < 16; i++)
                h = hash_u(h, (is_drum(trk) ? kit_has(&tp->st[ui.bar * 16 + i], ui.dsound + 1) : step_on(&tp->st[ui.bar * 16 + i], trk)));
        } else {
            h = hash_u(h, ui.bar); h = hash_u(h, cur_pat(trk)); h = hash_u(h, cued_pat(trk));
            h = hash_u(h, ui.dsound); h = hash_u(h, ui.rem[0]); h = hash_u(h, proj.tempo_x10);
            h = hash_u(h, is_drum(trk));
        }
        if (kids_sig[band_ix] == h) { y += height[band_ix]; continue; }
        kids_sig[band_ix] = h;
        kids_draws++;
        cv_begin(240, height[band_ix], bg);
        if (band_ix == 0) {
            kids_text(6, 5, kids_mode == 1 ? "STAGE" : "STEPS");
            kids_text(130, 5, recording() ? "RECORDING" : playing() ? "PLAYING" : "STOPPED");
        } else if (band_ix == 1 || band_ix == 2) {
            for (int i = 0; i < 4; i++) {
                int t = (band_ix - 1) * 4 + i, x = 5 + i * 59;
                uint16_t ink = THEMES[THEME_BW].ink[t];
                cv_rect(x, 2, 54, 22, trk == t ? fg : RGB(30, 30, 34));
                cv_rect(x + 2, 4, 50, 18, bg);
                /* Stable numbered marks: color is never the sole identity. */
                cv_rect(x + 5, 7, 5 + (t % 4) * 2, 10, ink);
                kids_number(x + 22, 5, t + 1);
                if (seq.mute & (1u << t)) kids_text(x + 38, 5, "M");
                if (t < 4) cv_rect(x + 2, 22, 50, 2, ink);
            }
        } else if (band_ix == 3 || band_ix == 4) {
            if (kids_mode == 1) {
                kids_text(8, 7, band_ix == 3 ? "SEQ  Make a pattern" : "SEL  Adult Studio");
            } else for (int i = 0; i < 8; i++) {
                int s = ui.bar * 16 + (band_ix - 3) * 8 + i, x = 5 + i * 29;
                int on = is_drum(trk) ? kit_has(&tp->st[s], ui.dsound + 1) : step_on(&tp->st[s], trk);
                cv_rect(x, 2, 26, 27, s < tp->len ? RGB(55, 55, 62) : RGB(22, 22, 26));
                if (on) cv_rect(x + 1, 3, 24, 3, THEMES[THEME_BW].ink[trk]);
                kids_number(x + 4, 8, s + 1);
                if (s == now) cv_rect(x + 1, 27, 24, 2, fg);
            }
        } else if (band_ix == 5) {
            kids_text(6, 0, "PAT"); kids_number(30, 0, cur_pat(trk) + 1);
            kids_text(60, 0, "NEXT"); kids_number(94, 0, cued_pat(trk) + 1);
            kids_text(132, 0, "BAR"); kids_number(160, 0, ui.bar + 1); kids_text(175, 0, "/4");
            kids_text(6, 16, "PRESETS pattern | OCT-/+ bar");
        } else if (band_ix == 6) {
            kids_text(6, 0, kids_mode == 2 ? "WHITE steps | HOME Stage" : "ALGORITHM track | PLAY run/stop");
            kids_text(6, 16, "SEL Adult Studio | not saved");
        } else {
            const char *labels[4] = {"K1 Track", "K2 Bar", is_drum(trk) ? "K3 Hit" : "K3 Note", "K4 BPM"};
            int values[4] = {trk + 1, ui.bar + 1, is_drum(trk) ? ui.dsound + 1 : ui.rem[0], proj.tempo_x10 / 10};
            for (int i = 0; i < 4; i++) {
                kids_text(5 + 59 * i, 2, labels[i]);
                kids_number(5 + 59 * i, 19, values[i]);
            }
        }
        cv_blit(0, y);
        y += height[band_ix];
    }
}
#endif
