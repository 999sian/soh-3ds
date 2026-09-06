#pragma once
#include <stdint.h>

/* A contact belongs to the button where it began. Moving off it cancels the
 * entire contact; mode/scene changes require release before another action. */
typedef struct Soh3dsQuickCapture {
    int initialized, contact, candidate, samples, blocked, active, pressed;
    uintptr_t context;
} Soh3dsQuickCapture;

static inline int Soh3dsQuickTarget(int x, int y) {
    if (y < 202 || y >= 238) return -1;
    for (int i = 0; i < 3; ++i) {
        int left = 10 + i * 102;
        if (x >= left && x < left + 96) return i;
    }
    return -1;
}

static inline void Soh3dsQuickCapture_Update(Soh3dsQuickCapture* s, int held, int target,
                                            int allowed, uintptr_t context) {
    s->pressed = 0;
    s->active = -1;
    if (!s->initialized || s->context != context) {
        s->initialized = 1;
        s->context = context;
        s->blocked = held;
        s->contact = 0;
    }
    if (!held) {
        s->contact = s->blocked = s->samples = 0;
        s->candidate = -1;
        return;
    }
    if (!allowed) s->blocked = 1;
    if (s->blocked) return;
    if (!s->contact) {
        s->contact = 1;
        s->candidate = target;
        s->samples = 0;
    }
    if (target < 0 || target > 2 || target != s->candidate) {
        s->blocked = 1;
        return;
    }
    if (s->samples < 3) ++s->samples;
    if (s->samples >= 2) {
        s->active = target;
        s->pressed = s->samples == 2;
    }
}
