#pragma once

#include <algorithm>
#include <cmath>

namespace ra2engine {

// A canvas `lh` lines high (RA2_LOGICAL_HEIGHT), `lh` times the window's
// aspect wide, kept between 16:9 and ~21:9. Narrower windows letterbox.
inline void logical_size_for_height(int lh, int window_w, int window_h, int& out_w, int& out_h) {
    const int lw = static_cast<int>(std::lround(static_cast<double>(lh) * window_w / window_h)) & ~1;
    out_h = lh;
    out_w = std::max(lh * 16 / 9, std::min(lh * 239 / 100, lw));
}

// HiDPI: the desktop match canvas is the window's pixels divided by a whole
// number k, so every canvas pixel is exactly k x k screen pixels (bitmap fonts
// and SHP art stay crisp, never 1.36 px wide). The sidebar is a fixed 168-pixel
// column made for 640x480 to 1024x768, so k keeps the canvas near 1080 lines:
// k = 1 up to 1299 lines (1080p, 1366x768, 1200p), k = 2 from 1300 to 2200
// (1440p and ultrawide 3440x1440: sidebar 336 screen pixels, 4K, Retina), k = 3
// from 2201 (5K 2880: 960 lines). Tactical zoom-out (zoom_min) gives back the
// wider world view. Below 720 lines the canvas stays 720 (scaled down). Width as
// logical_size_for_height, without its rounding to even, so k * canvas is the
// window to within k - 1 pixels.
inline int desktop_pixel_scale(int output_h) {
    if (output_h < 1300) return 1;
    return (output_h + 1099) / 1100;
}
inline void desktop_canvas(int output_w, int output_h, int& out_w, int& out_h) {
    const int k = desktop_pixel_scale(output_h);
    const int lh = std::max(720, output_h / k);
    const int lw = output_w / k;
    out_h = lh;
    out_w = std::max(lh * 16 / 9, std::min(lh * 239 / 100, lw));
}

// How a lw x lh canvas reaches an ow x oh window. `exact`: a whole scale k >= 1
// fills one axis to within k - 1 pixels, so SDL's integer scale is used (no
// resampling at all). Otherwise the scale is fractional (1280x720 menus on
// 1080p are 1.5x; a window smaller than the canvas is < 1): the frame is drawn
// into a canvas texture, enlarged `prescale` times with nearest sampling and
// then filtered down to the window (sharp bilinear: even pixel widths, at most
// one screen pixel of softening at the edges). prescale 1 = filter directly.
struct PresentMode {
    bool exact = true;
    int scale = 1;      // exact: the whole scale
    int prescale = 1;   // fractional: the nearest-neighbour step before filtering
    double fit = 1.0;   // window pixels per canvas pixel (the limiting axis)
};
inline PresentMode present_mode(int ow, int oh, int lw, int lh) {
    PresentMode m;
    if (ow <= 0 || oh <= 0 || lw <= 0 || lh <= 0) return m;
    m.fit = std::min(static_cast<double>(ow) / lw, static_cast<double>(oh) / lh);
    const int k = std::min(ow / lw, oh / lh);
    if (k >= 1 && (ow - k * lw < k || oh - k * lh < k)) {
        m.scale = k;
        return m;
    }
    m.exact = false;
    m.prescale = std::max(1, static_cast<int>(std::ceil(m.fit - 1e-9)));
    return m;
}

// Tactical zoom (presentation only: the camera, the simulation and the saves
// never see it). zoom = canvas pixels per world pixel, 1 = the original's
// size. Zooming out is limited so a world pixel never gets smaller than half
// a screen pixel's worth of detail: on a 1:1 screen 0.75 (tiles 45 px wide),
// at 2 screen pixels per canvas pixel (Retina, 4K) 0.5, which is the art at
// exactly one screen pixel per art pixel. Never below 0.5, never above 1.
inline double zoom_min(double screen_px_per_canvas_px) {
    const double s = screen_px_per_canvas_px > 0.0 ? screen_px_per_canvas_px : 1.0;
    return std::max(0.5, std::min(0.75, 1.0 / s));
}
// One wheel notch / key press: eighths (1, 0.875, 0.75, 0.625, 0.5), clamped.
inline double zoom_step(double zoom, int dir, double zmin) {
    double z = std::round((zoom + 0.125 * dir) * 8.0) / 8.0;
    return std::max(zmin, std::min(1.0, z));
}
// The world-view size that fills `canvas` canvas pixels at `zoom`.
inline int zoom_view(int canvas, double zoom) {
    return zoom >= 1.0 ? canvas : static_cast<int>(std::ceil(canvas / zoom - 1e-9));
}
// A canvas pixel coordinate (pointer) in the world view (what Input and the
// camera use). Exact inverse of drawing the world view scaled by `zoom`.
inline int zoom_to_view(int canvas_px, double zoom) {
    return zoom >= 1.0 ? canvas_px : static_cast<int>(std::floor(canvas_px / zoom));
}
// The camera after a zoom change that keeps the world point under canvas pixel
// `anchor` fixed (the pointer for the wheel, the view centre for keys).
inline int zoom_keep_point(int cam, int anchor, double z_old, double z_new) {
    return cam + static_cast<int>(std::lround(anchor / z_new - anchor / z_old));
}


// A pointer reported in canvas coordinates can lie in the letterbox bars (SDL
// maps the whole window, so a bar is x < 0 or x >= w). The software cursor and
// the edge scroll live on the canvas: keep the pointer on it, as cnc-ddraw
// does by clipping the cursor to the game area.
inline int clamp_pointer(int v, int size) {
    return size <= 0 ? v : std::max(0, std::min(size - 1, v));
}

// The edge-scroll band in canvas pixels: 24 screen pixels (the 1:1 band of a
// 1080p window), so at 2 or 3 screen pixels per canvas pixel it does not grow
// to 48 or 72 screen pixels of dead map. At most 24 canvas pixels (a window
// smaller than the canvas), at least 8.
inline int edge_scroll_band(double fit) {
    const double f = fit > 0.0 ? fit : 1.0;
    return std::max(8, std::min(24, static_cast<int>(std::lround(24.0 / f))));
}

// First window size on a display that scales its UI (Windows 125-300%): the
// per-monitor-DPI-aware window is real pixels, so a 1280x720 request would
// shrink to 1280/scale device-independent points. Scale by the display DPI in
// quarter steps (96 dpi = 1), and keep the window inside 90% of the display.
// ddpi <= 0 (unknown) or an explicit size from the caller: unchanged.
inline void scaled_window_size(int w, int h, double ddpi, int display_w, int display_h, int& out_w, int& out_h) {
    double s = ddpi > 0.0 ? std::round(ddpi / 96.0 * 4.0) / 4.0 : 1.0;
    s = std::max(1.0, std::min(4.0, s));
    if (display_w > 0) s = std::min(s, display_w * 0.9 / w);
    if (display_h > 0) s = std::min(s, display_h * 0.9 / h);
    s = std::max(1.0, s);
    out_w = static_cast<int>(std::lround(w * s));
    out_h = static_cast<int>(std::lround(h * s));
}

}  // namespace ra2engine
