// vlswm — wallpaper rendering: loads an image (via gdk-pixbuf), scales it to
// cover the output and shows it in the background scene layer.
#pragma once

#include "vlswm/wlroots.h"

struct Server;

// Create a scene buffer holding the wallpaper scaled to cover (w x h) pixels.
// The buffer is owned by the wallpaper system (per-server) and stays valid for
// the lifetime of the server. Returns nullptr on failure.
struct wlr_scene_buffer *wallpaper_create(Server *server, const char *path,
                                          int width, int height);

// Hand the decoded wallpaper raster to the server. Takes ownership of `data`
// (must be malloc'd, w*h*4 bytes of BGRA premultiplied pixels); a previous
// raster is freed. Bumps server->wp_rev.
void wallpaper_set_raster(Server *server, unsigned char *data, int w, int h);

// Drop the server's wallpaper raster (e.g. before reloading a new wallpaper).
void wallpaper_clear_raster(Server *server);

// Sample the wallpaper color at output coordinates (x, y), clamped to the
// raster bounds; writes B, G, R into out[3]. When no raster is available a
// solid white is returned (the plain fallback background color).
void wallpaper_sample(Server *server, int x, int y, unsigned char out[3]);