#include "platform.hpp"
#include <algorithm>
#include "touch_gestures.hpp"
#include "logical_canvas.hpp"

#include <SDL.h>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace ra2engine {

struct Platform::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    int w = 0;
    int h = 0;
    TouchGestures touch;   // G-70 gesture layer (fed by SDL_FINGER*)
    // HiDPI present (logical_canvas.hpp present_mode): an exact whole scale
    // goes through SDL's integer scale; a fractional one draws the frame into
    // `canvas`, enlarges it into `pre` with nearest sampling and filters that
    // down to the window. `hidden` (--play) always keeps the integer scale.
    bool hidden = false;
    int int_scale = -1;   // SDL_RenderSetIntegerScale state (-1 = not set yet)
    bool bound = false;   // `canvas` is the render target of this frame
    double fit = 1.0;     // window pixels per canvas pixel, last frame
    SDL_Texture* canvas = nullptr;
    SDL_Texture* pre = nullptr;
    int canvas_w = 0, canvas_h = 0, pre_w = 0, pre_h = 0;
    float wheel = 0.0f;   // fractional trackpad scroll not yet sent as a notch
    void drop_targets() {
        if (canvas) SDL_DestroyTexture(canvas);
        if (pre) SDL_DestroyTexture(pre);
        canvas = pre = nullptr;
        canvas_w = canvas_h = pre_w = pre_h = 0;
    }
    static SDL_Texture* target(SDL_Renderer* r, SDL_Texture*& t, int& tw, int& th, int w, int h) {
        if (t && tw == w && th == h) return t;
        if (t) SDL_DestroyTexture(t);
        t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
        tw = t ? w : 0;
        th = t ? h : 0;
        if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
        return t;
    }
    // W9-04-04: a phone returning from the background can deliver a mouse
    // event (no real mouse: the window corner) -- it switched the game to
    // mouse mode and edge-scrolled the map into the black. Mouse events are
    // dropped while the app is in the background, for 1 s after it returns
    // when the last pointer was a finger, and a button-less motion exactly at
    // the window corner right after a finger.
    bool background = false;
    uint32_t foreground_at = 0;
    bool last_touch = false;
    bool drop_mouse(int x, int y, bool motion_no_buttons) const {
        if (background) return true;
        if (!last_touch) return false;
        if (foreground_at && SDL_GetTicks() - foreground_at < 1000) return true;
        return motion_no_buttons && x == 0 && y == 0;
    }
    // Fullscreen with the focus: keep the pointer in the window, so edge
    // scrolling does not leave onto a second monitor (cnc-ddraw locks the
    // cursor too). Windowed play and a lost focus release it.
    void update_grab() const {
#ifndef __ANDROID__
        const Uint32 f = SDL_GetWindowFlags(window);
        SDL_SetWindowMouseGrab(window, ((f & SDL_WINDOW_FULLSCREEN_DESKTOP) && (f & SDL_WINDOW_INPUT_FOCUS)) ? SDL_TRUE : SDL_FALSE);
#endif
    }
    // W9-04-06: the tap / drag slop is Android's touch slop, 8 dp, in the
    // window (logical) pixels the fingers are reported in -- 12 px was ~4.6
    // dp on a 420-dpi phone, so a rolling fingertip turned taps into
    // ignored drags. Never below the old 12 px; desktop unchanged.
    void update_slop() {
#ifdef __ANDROID__
        float ddpi = 0.0f;
        int ow = 0, oh = 0;
        if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) != 0 || ddpi <= 0.0f) return;
        if (SDL_GetRendererOutputSize(renderer, &ow, &oh) != 0 || ow <= 0 || w <= 0) return;
        const double slop = 8.0 * ddpi / 160.0 * (static_cast<double>(w) / ow);
        touch.set_slop(std::max(12, static_cast<int>(slop + 0.5)));
#endif
    }
};

namespace {
std::string g_launch_title;
int g_launch_w = 0, g_launch_h = 0, g_launch_fs = -1;
}  // namespace

void Platform::set_launch_window(const std::string& title, int w, int h, int fullscreen) {
    g_launch_title = title;
    g_launch_w = w > 0 && h > 0 ? w : 0;
    g_launch_h = w > 0 && h > 0 ? h : 0;
    g_launch_fs = fullscreen;
}
bool Platform::launch_window_sized() { return g_launch_w > 0 || g_launch_fs >= 0; }

Platform Platform::create(const char* title, int w, int h, bool hidden) {
    if (!hidden && !g_launch_title.empty()) title = g_launch_title.c_str();
    // G-71: SDL must not synthesise mouse events from touches (every tap would
    // arrive twice = a double-click); fingers go through TouchGestures. The
    // SDL_TOUCH_MOUSEID filter in poll() backs this up.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    // Crisp upscaled UI / shell art: bilinear logical-size scaling blurs 1px
    // Westwood bitmap fonts and SHP pixels on Retina and other HiDPI windows.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    // Windows: per-monitor DPI aware, so a 150% display gets a real pixel
    // window instead of a 100% one the compositor stretches (blurry text).
    // Window sizes stay in pixels (no SDL_HINT_WINDOWS_DPI_SCALING). Other
    // systems ignore the hint.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(SDL_GetError());
    }

    Uint32 flags = hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
#ifndef __ANDROID__
    // macOS Retina and Wayland scaled outputs: a drawable in real pixels (the
    // window size stays in points there). X11 and Windows are pixels anyway.
    flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif
#ifdef __ANDROID__
    // W2-AR-06: SDLActivity only goes immersive (status + navigation bars
    // hidden, the whole panel) for a FULLSCREEN window; a plain SHOWN window
    // got 1794x1017 of a 1920x1080 phone with both bars visible.
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    if (!hidden && g_launch_fs == 1) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (!hidden && g_launch_w > 0) flags |= SDL_WINDOW_RESIZABLE;
    int win_w = !hidden && g_launch_w > 0 ? g_launch_w : w, win_h = !hidden && g_launch_h > 0 ? g_launch_h : h;
#ifdef _WIN32
    // A per-monitor-DPI-aware window is real pixels: without a size from the
    // launcher, a 150% display would get a window 2/3 the size it had when
    // Windows stretched it. (macOS / Wayland windows are in points already.)
    if (!hidden && g_launch_w <= 0) {
        float ddpi = 0.0f;
        SDL_Rect db{0, 0, 0, 0};
        if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 0.0f) {
            SDL_GetDisplayBounds(0, &db);
            scaled_window_size(w, h, ddpi, db.w, db.h, win_w, win_h);
        }
    }
#endif
    SDL_Window* window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        flags);
    if (!window) {
        std::string err = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error(err);
    }

    // G0 --play: a hidden window renders through the software renderer so the
    // captured pixels do not depend on the GPU driver.
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, hidden ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));   // W2-GA-P6 vsync (no tearing)
    if (!renderer && !hidden) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        std::string err = SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(err);
    }

    // Fix a LOGICAL render resolution (w x h, the size the engine's scene
    // layout/camera code is written against) decoupled from the window's
    // actual PHYSICAL pixel size. SDL scales all rendering to fill the real
    // window/framebuffer and transparently remaps mouse/finger event
    // coordinates into logical space for us. This matters because:
    //  - Desktop: SDL_CreateWindow honors (w, h) directly, so physical ==
    //    logical and this is a no-op in practice; resizing the window still
    //    scales correctly instead of clipping.
    //  - Android: SDL_CreateWindow is forced fullscreen at the device's real
    //    resolution (the requested w/h is ignored), so without a logical
    //    size the scene would render at 1280x720 in the corner of a larger
    //    framebuffer and touch coordinates (normalized 0..1, scaled by the
    //    logical w/h in Platform::poll) would no longer line up with what's
    //    drawn. Setting the logical size keeps rendering AND input in the
    //    same fixed coordinate space on both platforms.
    SDL_RenderSetLogicalSize(renderer, w, h);

    Platform p;
    p.p_ = new Impl{window, renderer, w, h, TouchGestures{}};
    p.p_->hidden = hidden;
    p.p_->update_slop();
    return p;
}

// G-89: RA2.INI [Video] ScreenWidth/ScreenHeight -> the window size; the engine
// keeps rendering at its logical size, scaled by SDL.
// W6-06-12: a size larger than the display or than the renderer's largest
// texture (a spanned multi-monitor size, a value from another machine) made
// every start fail (CreateTexture INVALIDCALL); it now falls back to 1280x720
// (fitted to the display) and returns false so the caller rewrites [Video].
// [RE-FALLBACK: the original's video init falls back to a supported mode.]
bool Platform::set_window_size(int w, int h) {
    if (!p_ || w <= 0 || h <= 0) return false;
    bool ok = true;
    int maxw = 0, maxh = 0;
    SDL_Rect db{0, 0, 0, 0};
    int di = SDL_GetWindowDisplayIndex(p_->window);
    if (di < 0) di = 0;
    if (SDL_GetDisplayBounds(di, &db) == 0 && db.w > 0 && db.h > 0) { maxw = db.w; maxh = db.h; }
    SDL_RendererInfo ri;
    if (SDL_GetRendererInfo(p_->renderer, &ri) == 0 && ri.max_texture_width > 0 && ri.max_texture_height > 0) {
        maxw = maxw > 0 ? std::min(maxw, ri.max_texture_width) : ri.max_texture_width;
        maxh = maxh > 0 ? std::min(maxh, ri.max_texture_height) : ri.max_texture_height;
    }
    if ((maxw > 0 && w > maxw) || (maxh > 0 && h > maxh)) {
        SDL_Log("[video] RA2.INI ScreenWidth x ScreenHeight %dx%d does not fit (max %dx%d): using 1280x720", w, h,
                maxw, maxh);
        ok = false;
        w = maxw > 0 ? std::min(1280, maxw) : 1280;
        h = maxh > 0 ? std::min(720, maxh) : 720;
    }
    SDL_SetWindowResizable(p_->window, SDL_TRUE);
    SDL_SetWindowSize(p_->window, w, h);
    SDL_SetWindowPosition(p_->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return ok;
}
void Platform::set_fullscreen(bool on) {
    if (!p_) return;
    SDL_SetWindowFullscreen(p_->window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    p_->update_grab();
}

void Platform::set_logical_size(int w, int h) {
    if (!p_ || w <= 0 || h <= 0 || (w == p_->w && h == p_->h)) return;
    p_->w = w;
    p_->h = h;
    SDL_RenderSetLogicalSize(p_->renderer, w, h);
    p_->update_slop();
}
void Platform::output_size(int& w, int& h) const {
    w = h = 0;
    if (!p_) return;
    // The window's drawable in pixels (Retina / Wayland: points x scale), also
    // while the canvas texture is the render target.
    SDL_GetWindowSizeInPixels(p_->window, &w, &h);
    if (w <= 0 || h <= 0) SDL_GetRendererOutputSize(p_->renderer, &w, &h);
}
double Platform::present_fit() const { return p_ ? p_->fit : 1.0; }

Platform::Platform(Platform&& other) noexcept : p_(other.p_) {
    other.p_ = nullptr;
}

Platform& Platform::operator=(Platform&& other) noexcept {
    if (this != &other) {
        if (p_) {
            SDL_DestroyRenderer(p_->renderer);
            SDL_DestroyWindow(p_->window);
            delete p_;
            SDL_Quit();
        }
        p_ = other.p_;
        other.p_ = nullptr;
    }
    return *this;
}

Platform::~Platform() {
    if (p_) {
        SDL_DestroyRenderer(p_->renderer);
        SDL_DestroyWindow(p_->window);
        delete p_;
        SDL_Quit();
    }
}

// SDL keycode -> Windows virtual-key code (KeyboardMD.ini [Hotkey] code space).
static int vk_from_sdl(int k) {
    if (k >= SDLK_a && k <= SDLK_z) return 'A' + (k - SDLK_a);
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + (k - SDLK_0);
    if (k >= SDLK_F1 && k <= SDLK_F12) return 112 + (k - SDLK_F1);
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return 97 + (k - SDLK_KP_1);   // VK_NUMPAD1..9
    switch (k) {
        case SDLK_ESCAPE: case SDLK_AC_BACK: return 27;   // Android BACK = Esc (06#38)
        case SDLK_SPACE: return 32;
        case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
        case SDLK_BACKSPACE: return 8;
        case SDLK_TAB: return 9;
        case SDLK_PAGEUP: return 33;
        case SDLK_PAGEDOWN: return 34;
        case SDLK_END: return 35;
        case SDLK_HOME: return 36;
        case SDLK_LEFT: return 37;
        case SDLK_UP: return 38;
        case SDLK_RIGHT: return 39;
        case SDLK_DOWN: return 40;
        case SDLK_INSERT: return 45;
        case SDLK_DELETE: return 46;
        case SDLK_KP_0: return 96;
        case SDLK_KP_MULTIPLY: return 106;
        case SDLK_KP_PLUS: return 107;
        case SDLK_KP_MINUS: return 109;
        case SDLK_KP_PERIOD: return 110;   // VK_DECIMAL ([Hotkey] Delete=110)
        case SDLK_KP_DIVIDE: return 111;
        default: return 0;
    }
}

void key_event_from_sdl(int k, int mod, InputEvent& ev) {
    ev.shift = (mod & KMOD_SHIFT) != 0;
    ev.ctrl = (mod & KMOD_CTRL) != 0;
    ev.alt = (mod & KMOD_ALT) != 0;
    ev.vk = vk_from_sdl(k);
    ev.key = 0;
    ev.system_back = (k == SDLK_AC_BACK);
    // P7: the named keys the save/load UI + P10 briefing consume.
    // G-01: Escape is an ordinary key (KeyboardMD.ini [Hotkey] Options=27 --
    // the in-game options dialog; dialogs treat it as Back). Only the window
    // close button (SDL_QUIT) quits the program.
    switch (k) {
        case SDLK_ESCAPE: case SDLK_AC_BACK: ev.key = kKeyEscape; break;
        case SDLK_BACKSPACE: ev.key = kKeyBackspace; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: ev.key = kKeyEnter; break;
        case SDLK_UP: ev.key = kKeyUp; break;
        case SDLK_DOWN: ev.key = kKeyDown; break;
        case SDLK_DELETE: ev.key = kKeyDelete; break;
        case SDLK_F10: ev.key = kKeyF10; break;
        case SDLK_F1: ev.key = kKeyF1; break;
        default: break;
    }
    ev.kind = (ev.key || ev.vk) ? InputEvent::Key : InputEvent::None;
}

bool Platform::touch_hold(int& x, int& y, float& progress) const {
    return p_ && p_->touch.hold_feedback(SDL_GetTicks(), x, y, progress);
}

bool Platform::poll(InputEvent& ev) {
    ev = InputEvent{};
    if (p_->touch.pop(ev)) return true;   // G-70: queued gesture events first
    SDL_Event e;
    if (!SDL_PollEvent(&e)) {
        p_->touch.tick(SDL_GetTicks());   // long-press fires with no new event
        if (p_->touch.pop(ev)) return true;
        ev.kind = InputEvent::None;
        return false;
    }

    switch (e.type) {
        case SDL_QUIT:
            ev.kind = InputEvent::Quit;
            break;
        case SDL_KEYDOWN:
            // G-89: Alt+Enter toggles desktop fullscreen (the logical 1280x720
            // layout is letterboxed by SDL_RenderSetLogicalSize either way).
            if (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT) && !e.key.repeat) {
                set_fullscreen(!(SDL_GetWindowFlags(p_->window) & SDL_WINDOW_FULLSCREEN_DESKTOP));
                ev.kind = InputEvent::None;
                break;
            }
            key_event_from_sdl(e.key.keysym.sym, e.key.keysym.mod, ev);
            ev.t_ms = e.key.timestamp;
            // A held command key must not re-fire (D would toggle deployers
            // every repeat); only the editing keys (kKey*) auto-repeat.
            if (e.key.repeat && ev.key == 0) ev.kind = InputEvent::None;
            break;
        // W2-AR-09/-26: Android lifecycle. SDL 2.30 sends these from the game
        // thread's PumpEvents and only blocks the thread AFTER the app has
        // consumed DIDENTERBACKGROUND, so handling WILLENTERBACKGROUND here
        // (autosave, pause) still runs before the process can be frozen.
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_TERMINATING:
            p_->background = true;
            ev.kind = InputEvent::AppBackground;
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            p_->background = false;
            p_->foreground_at = SDL_GetTicks();
            if (!p_->foreground_at) p_->foreground_at = 1;
            ev.kind = InputEvent::AppForeground;
            break;
        case SDL_APP_LOWMEMORY:
            ev.kind = InputEvent::AppLowMemory;
            break;
        case SDL_RENDER_DEVICE_RESET:
        case SDL_RENDER_TARGETS_RESET:
            p_->drop_targets();   // recreated by the next begin_frame
            ev.kind = InputEvent::RenderReset;
            break;
        case SDL_WINDOWEVENT:
            // R-162: foldables, split screen, a resized desktop window.
            // A window dragged to a monitor with another scale keeps its size in
            // points but not its pixels: the canvas is re-fitted for it too.
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
                p_->update_slop();
                ev.kind = InputEvent::Resize;
                SDL_GetWindowSize(p_->window, &ev.x, &ev.y);
            } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                p_->update_grab();
                ev.kind = InputEvent::FocusGained;
            } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                SDL_SetWindowMouseGrab(p_->window, SDL_FALSE);
                ev.kind = InputEvent::None;
            } else {
                ev.kind = InputEvent::None;
            }
            break;
        case SDL_MOUSEWHEEL: {
            // Trackpads send fractions of a notch: add them up, one event per
            // whole notch (a flick still gives several).
            float dy = e.wheel.preciseY;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
            p_->wheel += dy;
            const int notches = static_cast<int>(p_->wheel);
            p_->wheel -= static_cast<float>(notches);
            if (notches == 0) { ev.kind = InputEvent::None; break; }
            ev.kind = InputEvent::Wheel;
            ev.dy = notches;
            ev.x = clamp_pointer(e.wheel.mouseX, p_->w);
            ev.y = clamp_pointer(e.wheel.mouseY, p_->h);
            break;
        }
        case SDL_TEXTINPUT:
            ev.kind = InputEvent::Text;
            std::snprintf(ev.text, sizeof ev.text, "%s", e.text.text);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.which == SDL_TOUCH_MOUSEID) break;   // G-71: no synthetic duplicate
            if (p_->drop_mouse(e.button.x, e.button.y, false)) break;   // W9-04-04
            p_->last_touch = false;
            ev.kind = InputEvent::PointerDown;
            ev.x = clamp_pointer(e.button.x, p_->w);
            ev.y = clamp_pointer(e.button.y, p_->h);
            ev.button = (e.button.button == SDL_BUTTON_RIGHT) ? InputEvent::Right
                                                              : InputEvent::Left;
            ev.shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            ev.ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
            ev.alt = (SDL_GetModState() & KMOD_ALT) != 0;
            ev.t_ms = e.button.timestamp;
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.which == SDL_TOUCH_MOUSEID) break;
            if (p_->drop_mouse(e.button.x, e.button.y, false)) break;   // W9-04-04
            ev.kind = InputEvent::PointerUp;
            ev.x = clamp_pointer(e.button.x, p_->w);
            ev.y = clamp_pointer(e.button.y, p_->h);
            ev.button = (e.button.button == SDL_BUTTON_RIGHT) ? InputEvent::Right
                                                              : InputEvent::Left;
            ev.shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            ev.ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
            ev.alt = (SDL_GetModState() & KMOD_ALT) != 0;
            ev.t_ms = e.button.timestamp;
            break;
        case SDL_MOUSEMOTION:
            if (e.motion.which == SDL_TOUCH_MOUSEID) break;
            if (p_->drop_mouse(e.motion.x, e.motion.y, e.motion.state == 0)) break;   // W9-04-04
            p_->last_touch = false;
            ev.kind = InputEvent::PointerMove;
            ev.x = clamp_pointer(e.motion.x, p_->w);
            ev.y = clamp_pointer(e.motion.y, p_->h);
            ev.t_ms = e.motion.timestamp;
            ev.shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            ev.ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
            ev.alt = (SDL_GetModState() & KMOD_ALT) != 0;
            break;
        // G-70: fingers go through the gesture layer (tap = left click, drag =
        // band box, long-press / two-finger tap = right click, two-finger drag =
        // pan); the produced events are drained at the top of poll().
        case SDL_FINGERDOWN:
            p_->last_touch = true;
            p_->touch.finger_down(e.tfinger.fingerId, static_cast<int>(e.tfinger.x * p_->w),
                                  static_cast<int>(e.tfinger.y * p_->h), e.tfinger.timestamp);
            ev.kind = InputEvent::None;
            break;
        case SDL_FINGERUP:
            if (e.tfinger.pressure < 0.0f)   // W2-AR-18: SDLSurface marks ACTION_CANCEL
                p_->touch.finger_cancel(e.tfinger.fingerId, static_cast<int>(e.tfinger.x * p_->w),
                                        static_cast<int>(e.tfinger.y * p_->h), e.tfinger.timestamp);
            else
                p_->touch.finger_up(e.tfinger.fingerId, static_cast<int>(e.tfinger.x * p_->w),
                                    static_cast<int>(e.tfinger.y * p_->h), e.tfinger.timestamp);
            ev.kind = InputEvent::None;
            break;
        case SDL_FINGERMOTION:
            p_->touch.finger_move(e.tfinger.fingerId, static_cast<int>(e.tfinger.x * p_->w),
                                  static_cast<int>(e.tfinger.y * p_->h), e.tfinger.timestamp);
            ev.kind = InputEvent::None;
            break;
        default:
            ev.kind = InputEvent::None;
            break;
    }
    return true;
}

void Platform::begin_frame() {
    SDL_Renderer* r = p_->renderer;
    if (p_->bound) SDL_SetRenderTarget(r, nullptr);   // a frame that never presented
    p_->bound = false;
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(r, &ow, &oh);
    const PresentMode m = present_mode(ow, oh, p_->w, p_->h);
    p_->fit = m.fit;
    {
        static int n = 0;
        if (n++ < 3 || (n % 120) == 0)
            std::cerr << "[mm] begin_frame output=" << ow << "x" << oh << " logical=" << p_->w << "x" << p_->h
                      << " exact=" << m.exact << " scale=" << m.scale << " prescale=" << m.prescale
                      << " fit=" << m.fit << " dpi=" << SDL_GetWindowSizeInPixels(p_->window, &ow, &oh) << std::endl;
    }
    const int want_int = (p_->hidden || m.exact) ? 1 : 0;
    if (want_int != p_->int_scale) {
        SDL_RenderSetIntegerScale(r, want_int ? SDL_TRUE : SDL_FALSE);
        p_->int_scale = want_int;
    }
    if (!want_int && Impl::target(r, p_->canvas, p_->canvas_w, p_->canvas_h, p_->w, p_->h) &&
        SDL_SetRenderTarget(r, p_->canvas) == 0)
        p_->bound = true;
    // G-88: black, like the original (unmapped / shrouded area is black).
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
}

void Platform::present() {
    SDL_Renderer* r = p_->renderer;
    if (p_->bound) {
        p_->bound = false;
        SDL_Texture* src = p_->canvas;
        int ow = 0, oh = 0;
        SDL_SetRenderTarget(r, nullptr);
        SDL_GetRendererOutputSize(r, &ow, &oh);
        const PresentMode m = present_mode(ow, oh, p_->w, p_->h);
        if (m.prescale > 1 &&
            Impl::target(r, p_->pre, p_->pre_w, p_->pre_h, p_->w * m.prescale, p_->h * m.prescale) &&
            SDL_SetRenderTarget(r, p_->pre) == 0) {
            SDL_SetTextureScaleMode(p_->canvas, SDL_ScaleModeNearest);
            SDL_RenderCopy(r, p_->canvas, nullptr, nullptr);
            SDL_SetRenderTarget(r, nullptr);
            src = p_->pre;
        }
        SDL_SetTextureScaleMode(src, SDL_ScaleModeLinear);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        const SDL_Rect dst{0, 0, p_->w, p_->h};   // logical: SDL letterboxes it
        SDL_RenderCopy(r, src, nullptr, &dst);
    }
    SDL_RenderPresent(r);
}

void* Platform::renderer() const {
    return p_->renderer;
}

int Platform::width() const {
    return p_->w;
}

int Platform::height() const {
    return p_->h;
}

}  // namespace ra2engine
