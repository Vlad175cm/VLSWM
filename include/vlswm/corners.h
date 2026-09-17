// vlswm — rounded window corners: four small corner masks per view painted
// with the wallpaper color, so windows appear rounded even though wlroots has
// no native corner radius.
#pragma once

#include "vlswm/wlroots.h"

struct Server;
struct View;

// Effective corner radius for a view (0 → no rounding: unmapped or fullscreen,
// or `wm.corner_radius` not configured).
int corner_radius(Server *server, View *view);

// (Re)build the view's corner masks if its geometry, radius, or the wallpaper
// changed since the last call. Safe to call every frame (cheap when cached).
void corners_update(Server *server, View *view);

// Set the opacity of all four corner masks (so they fade in/out with the window).
void corners_opacity(View *view, float opacity);

// Destroy the view's corner masks (call on unmap / pre-destroy).
void corners_destroy(View *view);