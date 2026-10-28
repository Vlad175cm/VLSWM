#pragma once

#include <cstdint>

enum class Easing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
};

Easing easing_from_string(const char *name, Easing def = Easing::EaseOut);

uint64_t anim_now_ns();

double anim_ease(Easing e, double t);

struct FadeAnim {
    bool active = false;
    uint64_t start = 0;
    uint64_t duration = 0;
    float from = 1.0f;
    float to = 1.0f;
    Easing easing = Easing::EaseOut;

    void begin(uint64_t now, uint64_t dur, Easing e, float f, float t);
    bool step(uint64_t now, float *out);
};
