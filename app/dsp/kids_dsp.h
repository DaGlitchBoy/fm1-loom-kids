/* SPDX-License-Identifier: MPL-2.0 */
#ifndef LOOM_KIDS_DSP_H
#define LOOM_KIDS_DSP_H

#include "fastmath.h"
#include <stdint.h>

enum {
    KD_ENV_ON, KD_ATTACK, KD_HOLD, KD_SUSTAIN, KD_RELEASE,
    KD_LFO_ON, KD_TARGET, KD_DEPTH, KD_WAVE,
    KD_EDIT_ON, KD_LOWPASS, KD_DRIVE, KD_CRUSH, KD_CHORUS,
    KD_COUNT
};
enum { KD_VOLUME, KD_CUTOFF, KD_DISTORTION, KD_TARGET_COUNT };
enum { KD_SINE, KD_TRIANGLE, KD_SAW, KD_SQUARE, KD_WAVE_COUNT };
enum { KD_ATTACKING, KD_HOLDING, KD_DECAYING, KD_SUSTAINING, KD_RELEASING, KD_DONE };
#define KD_DECAY_MS 60
#define KD_FRAMES_PER_MS 44.1f

typedef struct {
    uint8_t value[KD_COUNT];
    float attack, hold, release;  /* frames */
    float sustain;
} kids_dsp_track_t;

extern kids_dsp_track_t kids_dsp_tracks[];

typedef struct {
    float level, from, elapsed;
    uint8_t phase;
} kids_gate_t;

static inline int kids_envelope_ms(int parameter, int value)
{
    if (parameter == KD_ATTACK) return 2 + 4 * value;
    if (parameter == KD_HOLD) return 10 * value;
    return 5 + 10 * value;
}

/* The same stages feed the synth, kit gain and graph. Updates are in audio frames.
 * A release starts from the actual gain reached at note-off, even halfway up the attack. */
static inline float kids_gate_advance(kids_gate_t *g, const kids_dsp_track_t *p,
                                      int released, float frames)
{
    if (released && g->phase < KD_RELEASING) {
        g->from = g->level;
        g->elapsed = 0;
        g->phase = KD_RELEASING;
    }
    while (frames > 0.0f) {
        float duration, target;
        switch (g->phase) {
        case KD_ATTACKING: duration = p->attack; target = 1.0f; break;
        case KD_HOLDING: duration = p->hold; target = 1.0f; break;
        case KD_DECAYING: duration = KD_DECAY_MS * KD_FRAMES_PER_MS; target = p->sustain; break;
        case KD_SUSTAINING: return g->level = p->sustain;
        case KD_RELEASING: duration = p->release; target = 0.0f; break;
        default: return 0.0f;
        }
        if (g->elapsed + frames < duration) {
            g->elapsed += frames;
            return g->level = g->from + (target - g->from) * (g->elapsed / duration);
        }
        frames -= fm_maxf(0.0f, duration - g->elapsed);
        g->elapsed = 0;
        g->level = g->from = target;
        g->phase++;
    }
    return g->level;
}

static inline float kids_lfo_shape(int wave, float phase)
{
    switch (wave) {
    case KD_TRIANGLE: return 1.0f - 4.0f * fm_fabsf(phase - 0.5f);
    case KD_SAW: return 2.0f * phase - 1.0f;
    case KD_SQUARE: return phase < 0.5f ? 1.0f : -1.0f;
    default: return fm_sin_turns(phase);
    }
}
#endif
