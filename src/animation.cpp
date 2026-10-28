
#include "vlswm/animation.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstring>

uint64_t anim_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

double anim_ease(Easing e, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
        case Easing::Linear:    return t;
        case Easing::EaseIn:    return t * t * t;
        case Easing::EaseOut:   return 1.0 - std::pow(1.0 - t, 3.0);
        case Easing::EaseInOut:
            return t < 0.5
                ? 4.0 * t * t * t
                : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    }
    return t;
}

Easing easing_from_string(const char *name, Easing def) {
    if (!name || !*name) return def;
    if (std::strcmp(name, "linear")     == 0) return Easing::Linear;
    if (std::strcmp(name, "ease-in")    == 0) return Easing::EaseIn;
    if (std::strcmp(name, "ease-out")   == 0) return Easing::EaseOut;
    if (std::strcmp(name, "ease-in-out") == 0) return Easing::EaseInOut;
    return def;
}



void FadeAnim::begin(uint64_t now, uint64_t dur, Easing e, float f, float t) {
    start = now;
    duration = dur;
    easing = e;
    from = f;
    to = t;
    active = true;
}

bool FadeAnim::step(uint64_t now, float *out) {
    if (!active) { *out = to; return true; }
    double t = duration ? (double)(now - start) / (double)duration : 1.0;
    bool done = (t >= 1.0);
    t = std::clamp(t, 0.0, 1.0);
    double e = anim_ease(easing, t);
    *out = from + (to - from) * (float)e;
    if (done) active = false;
    return done;
}
