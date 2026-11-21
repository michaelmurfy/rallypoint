#pragma once
// G-70 / G-71 / W4-AM-01: touch gesture layer (Android). Pure C++17, no SDL --
// the Platform feeds it finger events and drains the InputEvents it produces.
//
// The original is a mouse game; its scheme (left = select AND order, right =
// cancel/deselect, right-drag = scroll; see input_original.cpp) maps onto a
// finger like this (W4-AM-01, the common mobile-RTS convention) [RE-FALLBACK:
// no original touch mapping exists; the slop is Android's touch slop (8 dp
// ~ 12 logical px); the hold is 450 ms (W6-01-11: it was 600 ms while a slow
// tap could deselect -- W4-AM-07 made a hold never deselect, so the hold now
// sits inside Android's own 400-500 ms long-press window)]:
//   tap                         -> left click (select / order / sidebar / menu)
//   one-finger drag             -> Pan (the map follows the finger)
//   hold, then drag             -> left band box from the hold point (box select)
//   hold, then lift             -> right click marked `hold` (cancel the armed
//                                  mode; on a cameo: hold / abandon; on a team
//                                  button: create the team). It NEVER deselects
//                                  units (W4-AM-07).
//   two-finger drag             -> Pan
//   two-finger tap              -> right click (cancel a mode, else deselect)
//   three-finger tap            -> Key kKeyPerfToggle (R-170: the FPS / perf
//                                  overlay; the scene driver eats it)
//   W6-01-11: while BOX mode is on (the sticky [BOX] button of the command
//   column, set_box_mode), a one-finger drag draws the band box at once (no
//   hold); two fingers still scroll.
// Every emitted event has touch = true (GameScene then never edge-scrolls).
// hold_feedback() reports the hold progress so the scene can draw a ring at
// the finger before the hold fires.
#include <cstdint>
#include <deque>

#include "platform.hpp"

namespace ra2engine {

class TouchGestures {
public:
    struct Config {
        uint32_t long_press_ms = 450;
        int slop_px = 12;
        uint32_t two_finger_tap_ms = 300;
        uint32_t feedback_delay_ms = 150;   // the hold ring appears after this
        uint32_t three_finger_tap_ms = 500;
    };
    TouchGestures() = default;
    explicit TouchGestures(Config c) : cfg_(c) {}
    // W9-04-06: the Platform scales the slop to 8 dp on a phone.
    void set_slop(int px) { if (px > 0) cfg_.slop_px = px; }
    int slop() const { return cfg_.slop_px; }

    void finger_down(int64_t id, int x, int y, uint32_t t_ms);
    void finger_move(int64_t id, int x, int y, uint32_t t_ms);
    void finger_up(int64_t id, int x, int y, uint32_t t_ms);
    // W2-AR-18: the system took the gesture (back-swipe, notification pull):
    // a pending tap / hold / two-finger tap produces NO click; a band box
    // already pressed is released where it is.
    void finger_cancel(int64_t id, int x, int y, uint32_t t_ms);
    // Time passing with no finger event (hold detection).
    void tick(uint32_t now_ms);

    // W4-AM-07: the hold ring. True while one finger rests (within the slop)
    // past feedback_delay_ms; progress 0..1 reaches 1 when the hold fires.
    bool hold_feedback(uint32_t now_ms, int& x, int& y, float& progress) const;

    // W6-01-11: the sticky BOX toggle, shared by every gesture layer (the
    // Platform's and the --play harness's); GameScene's [BOX] button sets it.
    static void set_box_mode(bool on) { box_mode_ = on; }
    static bool box_mode() { return box_mode_; }

    bool pending() const { return !out_.empty(); }
    bool pop(InputEvent& ev) {
        if (out_.empty()) return false;
        ev = out_.front();
        out_.pop_front();
        return true;
    }

private:
    enum class State { Idle, Pending, Panning, Held, BandDrag, WaitUp, TwoFinger };
    void emit(InputEvent::Kind k, int x, int y, bool right, uint32_t t);
    void emit_click(int x, int y, bool right, uint32_t t, bool hold = false);
    void emit_pan(int dx, int dy, int x, int y, uint32_t t);

    Config cfg_;
    State st_ = State::Idle;
    int64_t id0_ = -1, id1_ = -1;
    int x0_ = 0, y0_ = 0, cx_ = 0, cy_ = 0;   // first finger: start / current
    int px_ = 0, py_ = 0;                     // one-finger pan: last emitted point
    int x1_ = 0, y1_ = 0;                     // second finger current
    uint32_t t0_ = 0;
    int mid_x_ = 0, mid_y_ = 0;               // two-finger centroid (last emitted)
    int travel_ = 0;                          // two-finger centroid travel (px)
    uint32_t t2_ = 0;                         // two-finger start time
    bool three_ = false;                      // R-170: a third finger joined
    int up_left_ = 0;                         // fingers still down after a 3-finger lift
    std::deque<InputEvent> out_;
    inline static bool box_mode_ = false;
};

}  // namespace ra2engine
