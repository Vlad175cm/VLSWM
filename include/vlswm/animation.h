// vlswm — animation engine: easing, scalar fades, geometry transitions.
#pragma once

#include <cstdint>
#include <string>

enum class Easing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
};

Easing easing_from_string(const char *name, Easing def = Easing::EaseOut);

// Returns nanoseconds from CLOCK_MONOTONIC.
uint64_t anim_now_ns();

// Maps t in [0, 1] → value in [0, 1] using the given easing curve.
double anim_ease(Easing e, double t);

// Smoothly interpolated geometry transition (x, y, w, h).
struct GeometryAnim {
    bool active = false;
    uint64_t start = 0;
    uint64_t duration = 0;
    int fx = 0, fy = 0, fw = 0, fh = 0;
    int tx = 0, ty = 0, tw = 0, th = 0;
    Easing easing = Easing::EaseOut;

    void begin(uint64_t now, uint64_t dur, Easing e,
               int cx, int cy, int cw, int ch,
               int nx, int ny, int nw, int nh);

    // Returns true when the transition is complete; writes current values.
    bool step(uint64_t now, int *x, int *y, int *w, int *h);
};

// Smooth scalar fade (for opacity).
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
