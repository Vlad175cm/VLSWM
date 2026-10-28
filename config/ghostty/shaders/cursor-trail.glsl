const float TRAIL_DURATION = 0.30;
const float TRAIL_OPACITY = 0.50;
const int TRAIL_STEPS = 8;

float trail_sd_box(vec2 p, vec2 center, vec2 half_size) {
    vec2 d = abs(p - center) - half_size;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec4 screen = texture(iChannel0, fragCoord / iResolution.xy);

    if (iFocus == 0 || iCursorVisible < 1) {
        fragColor = screen;
        return;
    }

    float age = clamp((iTime - iTimeCursorChange) / TRAIL_DURATION, 0.0, 1.0);
    float fade = 1.0 - age;
    if (fade <= 0.0) {
        fragColor = screen;
        return;
    }

    vec2 cur = (iCurrentCursor.xy * 2.0 - iResolution.xy) / iResolution.y;
    vec2 prev = (iPreviousCursor.xy * 2.0 - iResolution.xy) / iResolution.y;
    vec2 size = iCurrentCursor.zw * 2.0 / iResolution.y;
    vec2 half_size = size * 0.5;
    vec2 cur_center = cur + vec2(half_size.x, -half_size.y);
    vec2 prev_center = prev + vec2(half_size.x, -half_size.y);

    if (distance(cur_center, prev_center) < half_size.y * 0.5) {
        fragColor = screen;
        return;
    }

    vec2 p = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    float pixel = 2.0 / iResolution.y;

    vec2 lo = min(cur_center, prev_center) - half_size - vec2(pixel * 2.0);
    vec2 hi = max(cur_center, prev_center) + half_size + vec2(pixel * 2.0);
    if (p.x < lo.x || p.y < lo.y || p.x > hi.x || p.y > hi.y) {
        fragColor = screen;
        return;
    }

    vec3 trail = dot(iBackgroundColor, vec3(0.2126, 0.7152, 0.0722)) > 0.5
        ? vec3(0.0)
        : vec3(1.0);

    vec3 result = screen.rgb;
    for (int i = 1; i < TRAIL_STEPS; i++) {
        float t = float(i) / float(TRAIL_STEPS);
        vec2 center = mix(prev_center, cur_center, t);
        float d = trail_sd_box(p, center, half_size);
        float mask = 1.0 - smoothstep(-pixel, pixel, d);
        float alpha = fade * (1.0 - t) * TRAIL_OPACITY;
        result = mix(result, trail, mask * alpha);
    }

    fragColor = vec4(result, screen.a);
}
