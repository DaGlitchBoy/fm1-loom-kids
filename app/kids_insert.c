/* SPDX-License-Identifier: MPL-2.0 */
/* Kids-only DSP, included by engine.c after the native mixer helpers. */

#define KIDS_CHORUS_LINE 1024u
typedef struct {
    float low[2], held[2], chorus[KIDS_CHORUS_LINE];
    unsigned write, valid, crush_clock, tail;
} kids_insert_t;

static kids_insert_t kids_insert[NTRK];
static float kids_mix[NTRK][4][STRETCH_MAX];
static uint32_t kids_mix_mask;
static float kids_phase[NTRK], kids_phase_step, kids_chorus_phase;
static volatile uint8_t kids_phase_pixel[NTRK];
static uint8_t kids_phase_playing;

static void kids_insert_reset(int track)
{
    kids_insert_t *s = &kids_insert[track];
    s->valid = s->crush_clock = s->tail = 0;
    s->low[0] = s->low[1] = 0;
}

static int kids_insert_active(const kids_dsp_track_t *p)
{
    const uint8_t *v = p->value;
    return (v[KD_LFO_ON] && v[KD_DEPTH] != KD_DEPTH_ZERO) ||
        (v[KD_EDIT_ON] && (v[KD_LOWPASS] || v[KD_DRIVE] || v[KD_CRUSH] || v[KD_CHORUS]));
}

static void kids_mix_begin(uint32_t n)
{
    kids_mix_mask = 0;
    if (!seq.kids_mode) return;
    int restart = seq.playing && !kids_phase_playing;
    kids_phase_playing = seq.playing;
    for (int t = 0; t < NTRK; t++) {
        if (restart) kids_phase[t] = 0;
        kids_phase_pixel[t] = (uint8_t)(kids_phase[t] * 192.0f);
        if (eng_drum[t] || !kids_insert_active(&kids_dsp_tracks[t])) continue;
        kids_mix_mask |= 1u << t;
        for (int c = 0; c < 4; c++) zerof(kids_mix[t][c], n);
    }
}

static float kids_chorus_read(const kids_insert_t *s, float delay)
{
    unsigned whole = (unsigned)delay;
    if (s->valid <= whole + 1u) return 0;
    float a = s->chorus[(s->write - whole) & (KIDS_CHORUS_LINE - 1u)];
    float b = s->chorus[(s->write - whole - 1u) & (KIDS_CHORUS_LINE - 1u)];
    return a + (b - a) * (delay - (float)whole);
}

static float kids_crush_sample(float x)
{
    float step = fm_clampf(x, -0.5f, 0.484375f) * 64.0f;
    return (float)(int)(step + (step >= 0.0f ? 0.5f : -0.5f)) * (1.0f / 64.0f);
}

static void kids_track_insert(int t, float *l, float *r, float *send1, float *send2, uint32_t n)
{
    const uint8_t *p = kids_dsp_tracks[t].value;
    kids_insert_t *s = &kids_insert[t];
    if (!kids_insert_active(&kids_dsp_tracks[t])) {
        s->valid = s->crush_clock = 0;
        s->low[0] = s->low[1] = 0;
        return;
    }
    const float unit = 1.0f / 127.0f;
    const float lowpass = p[KD_EDIT_ON] ? p[KD_LOWPASS] * unit : 0.0f;
    const float drive = p[KD_EDIT_ON] ? p[KD_DRIVE] * unit : 0.0f;
    const float crush = p[KD_EDIT_ON] ? p[KD_CRUSH] * unit : 0.0f;
    const float chorus = p[KD_EDIT_ON] ? p[KD_CHORUS] * unit * 0.32f : 0.0f;
    const int signed_depth = p[KD_LFO_ON] ? p[KD_DEPTH] - KD_DEPTH_ZERO : 0;
    const float depth = (float)signed_depth * unit;
    const int target = p[KD_TARGET], wave = p[KD_WAVE];
    float phase = kids_phase[t];
    float phase_step = kids_phase_step * kids_lfo_cycles_per_bar[p[KD_RATE]];
    float warp = chorus ? 95.0f * fm_sin_turns(kids_chorus_phase) : 0.0f;
    if (!chorus) s->valid = 0;
    if (!crush) s->crush_clock = 0;
    /* Tremolo alone only needs a gain. Keep that path out of the insert chain. */
    if (target == KD_VOLUME && !(lowpass || drive || crush || chorus)) {
        float half_depth = 0.5f * depth;
        float base = 1.0f - fm_fabsf(half_depth);
        for (uint32_t i = 0; i < n; i++) {
            float gain = base + kids_lfo_shape(wave, phase) * half_depth;
            l[i] *= gain;
            r[i] *= gain;
            send1[i] *= gain;
            send2[i] *= gain;
            phase += phase_step;
            if (phase >= 1.0f) phase -= 1.0f;
        }
        return;
    }
    int filter = lowpass || (depth && target == KD_CUTOFF);
    for (uint32_t i = 0; i < n; i++) {
        float shape = depth ? kids_lfo_shape(wave, phase) * depth : 0.0f;
        float wet = 0.5f * (fm_fabsf(depth) + (target == KD_DISTORTION ? shape : -shape));
        phase += phase_step;
        if (phase >= 1.0f) phase -= 1.0f;
        float cutoff = target == KD_CUTOFF ? wet : 0.0f;
        float low = lowpass + cutoff * (1.0f - lowpass);
        float pole = 0.105f - 0.080f * cutoff;
        float dist = target == KD_DISTORTION ? drive + wet * (1.0f - drive) : drive;
        float gain = target == KD_VOLUME ? 1.0f - wet : 1.0f;
        float x[2] = {l[i], r[i]};
        for (int c = 0; c < 2; c++) {
            if (filter) {
                s->low[c] += pole * (x[c] - s->low[c]); /* ~780 Hz, down to ~180 Hz */
                x[c] += (s->low[c] - x[c]) * low;
            }
            if (dist) {
                float y = 1.7f * x[c] / (1.0f + 6.0f * fm_fabsf(x[c]));
                x[c] += (y - x[c]) * dist;
            }
            if (crush) {
                if (!s->crush_clock) s->held[c] = kids_crush_sample(x[c]);
                x[c] += (s->held[c] - x[c]) * crush;
            }
        }
        if (crush && ++s->crush_clock == 5u) s->crush_clock = 0;
        if (chorus) {
            s->chorus[s->write] = 0.5f * (x[0] + x[1]);
            x[0] += (kids_chorus_read(s, 365.0f + warp) - 0.35f * x[0]) * chorus;
            x[1] += (kids_chorus_read(s, 490.0f - warp) - 0.35f * x[1]) * chorus;
            s->write = (s->write + 1u) & (KIDS_CHORUS_LINE - 1u);
            if (s->valid < KIDS_CHORUS_LINE) s->valid++;
        }
        l[i] = x[0] * gain;
        r[i] = x[1] * gain;
        send1[i] *= gain;
        send2[i] *= gain;
    }
}

static void kids_mix_finish(uint32_t n)
{
    if (!seq.kids_mode) return;
    for (int t = 0; t < NTRK; t++) {
        if (!(kids_mix_mask >> t & 1u)) continue;
        float (*b)[STRETCH_MAX] = kids_mix[t];
        kids_track_insert(t, b[0], b[1], b[2], b[3], n);
        add_in(mel_l, b[0], n);
        add_in(mel_r, b[1], n);
        add_in(s1, b[2], n);
        add_in(s2, b[3], n);
    }
    for (int t = 0; t < NTRK; t++) {
        kids_phase[t] += kids_phase_step * kids_lfo_cycles_per_bar[kids_dsp_tracks[t].value[KD_RATE]] * (float)n;
        if (kids_phase[t] >= 1.0f) kids_phase[t] -= 1.0f;
    }
    kids_chorus_phase += (0.75f / SY_SR) * (float)n;
    if (kids_chorus_phase >= 1.0f) kids_chorus_phase -= 1.0f;
}
