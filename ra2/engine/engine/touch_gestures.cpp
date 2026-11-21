#include "touch_gestures.hpp"

#include <cstdlib>

namespace ra2engine {

void TouchGestures::emit(InputEvent::Kind k, int x, int y, bool right, uint32_t t) {
    InputEvent e;
    e.kind = k;
    e.x = x;
    e.y = y;
    e.button = right ? InputEvent::Right : InputEvent::Left;
    e.t_ms = t;
    e.touch = true;
    out_.push_back(e);
}

void TouchGestures::emit_click(int x, int y, bool right, uint32_t t, bool hold) {
    emit(InputEvent::PointerMove, x, y, false, t);
    emit(InputEvent::PointerDown, x, y, right, t);
    emit(InputEvent::PointerUp, x, y, right, t);
    if (hold) {   // W4-AM-07: a hold-release right click never deselects
        out_[out_.size() - 2].hold = true;
        out_.back().hold = true;
    }
}

void TouchGestures::emit_pan(int dx, int dy, int x, int y, uint32_t t) {
    if (!dx && !dy) return;
    InputEvent e;
    e.kind = InputEvent::Pan;
    e.dx = dx;   // the map follows the finger(s)
    e.dy = dy;
    e.x = x;
    e.y = y;
    e.x0 = x0_;   // where the gesture started (sidebar / radar routing)
    e.y0 = y0_;
    e.t_ms = t;
    e.touch = true;
    out_.push_back(e);
}

void TouchGestures::finger_down(int64_t id, int x, int y, uint32_t t) {
    switch (st_) {
        case State::Idle:
            st_ = State::Pending;
            up_left_ = 0;
            three_ = false;
            id0_ = id;
            x0_ = cx_ = px_ = x;
            y0_ = cy_ = py_ = y;
            t0_ = t;
            return;
        case State::Pending:
        case State::Panning:
            // A second finger: two-finger gesture (a one-finger pan simply
            // continues as a two-finger pan).
            st_ = State::TwoFinger;
            id1_ = id;
            x1_ = x;
            y1_ = y;
            mid_x_ = (cx_ + x1_) / 2;
            mid_y_ = (cy_ + y1_) / 2;
            travel_ = 0;
            t2_ = t;
            three_ = false;
            return;
        case State::TwoFinger:
            three_ = true;   // R-170: a third finger -> maybe a three-finger tap
            return;
        default:
            return;   // extra fingers during a band box / hold are ignored
    }
}

void TouchGestures::finger_move(int64_t id, int x, int y, uint32_t t) {
    if (st_ == State::TwoFinger) {
        if (id == id0_) { cx_ = x; cy_ = y; }
        else if (id == id1_) { x1_ = x; y1_ = y; }
        else return;
        const int mx = (cx_ + x1_) / 2, my = (cy_ + y1_) / 2;
        const int dx = mx - mid_x_, dy = my - mid_y_;
        if (dx || dy) {
            emit_pan(dx, dy, mx, my, t);
            travel_ += std::abs(dx) + std::abs(dy);
            mid_x_ = mx;
            mid_y_ = my;
        }
        return;
    }
    if (id != id0_) return;
    cx_ = x;
    cy_ = y;
    const bool beyond = std::abs(x - x0_) > cfg_.slop_px || std::abs(y - y0_) > cfg_.slop_px;
    if (st_ == State::Pending && beyond && box_mode_) {
        st_ = State::BandDrag;   // W6-01-11: BOX mode -- the drag is a band box
        emit(InputEvent::PointerMove, x0_, y0_, false, t0_);
        emit(InputEvent::PointerDown, x0_, y0_, false, t0_);
        emit(InputEvent::PointerMove, x, y, false, t);
        return;
    }
    if (st_ == State::Pending && beyond) {
        st_ = State::Panning;   // W4-AM-01: one-finger drag scrolls
        emit_pan(x - px_, y - py_, x, y, t);
        px_ = x;
        py_ = y;
        return;
    }
    if (st_ == State::Panning) {
        emit_pan(x - px_, y - py_, x, y, t);
        px_ = x;
        py_ = y;
        return;
    }
    if (st_ == State::Held && beyond) {
        st_ = State::BandDrag;   // hold-then-drag: band box from the hold point
        emit(InputEvent::PointerMove, x0_, y0_, false, t0_);
        emit(InputEvent::PointerDown, x0_, y0_, false, t0_);
    }
    if (st_ == State::BandDrag) emit(InputEvent::PointerMove, x, y, false, t);
}

void TouchGestures::finger_cancel(int64_t id, int x, int y, uint32_t t) {
    switch (st_) {
        case State::Pending:
        case State::Panning:
        case State::Held:
            if (id == id0_) st_ = State::Idle;
            return;
        case State::WaitUp:
            if (up_left_ > 0) {
                if (--up_left_ == 0) st_ = State::Idle;
                return;
            }
            if (id == id0_) st_ = State::Idle;
            return;
        case State::BandDrag:
            if (id != id0_) return;
            emit(InputEvent::PointerUp, x, y, false, t);
            st_ = State::Idle;
            return;
        case State::TwoFinger:
            if (three_) { three_ = false; st_ = State::WaitUp; up_left_ = 2; return; }
            if (id != id0_ && id != id1_) return;
            st_ = State::WaitUp;   // wait for the other finger, no click
            id0_ = (id == id0_) ? id1_ : id0_;
            return;
        default:
            return;
    }
}

void TouchGestures::finger_up(int64_t id, int x, int y, uint32_t t) {
    switch (st_) {
        case State::Pending:
            if (id != id0_) return;
            emit_click(x0_, y0_, false, t);   // tap
            st_ = State::Idle;
            return;
        case State::Panning:
            if (id == id0_) st_ = State::Idle;
            return;
        case State::WaitUp:
            if (up_left_ > 0) {   // after a three-finger lift: any two lifts
                if (--up_left_ == 0) st_ = State::Idle;
                return;
            }
            if (id == id0_) st_ = State::Idle;
            return;
        case State::Held:
            if (id != id0_) return;
            emit_click(x0_, y0_, true, t, /*hold=*/true);   // hold-release = right click
            st_ = State::Idle;
            return;
        case State::BandDrag:
            if (id != id0_) return;
            emit(InputEvent::PointerUp, x, y, false, t);
            st_ = State::Idle;
            return;
        case State::TwoFinger: {
            if (three_) {
                // R-170: the first lift of a three-finger gesture decides it;
                // the other two fingers' lifts are swallowed (WaitUp x2).
                if (travel_ <= cfg_.slop_px * 2 && t - t2_ <= cfg_.three_finger_tap_ms) {
                    InputEvent e;
                    e.kind = InputEvent::Key;
                    e.key = kKeyPerfToggle;
                    e.t_ms = t;
                    e.touch = true;
                    out_.push_back(e);
                }
                three_ = false;
                st_ = State::WaitUp;
                up_left_ = 2;
                return;
            }
            if (id != id0_ && id != id1_) return;
            // Two-finger TAP (short, no pan) = right click at the first finger.
            if (travel_ <= cfg_.slop_px && t - t2_ <= cfg_.two_finger_tap_ms)
                emit_click(x0_, y0_, true, t);
            // Wait for the other finger without re-entering a one-finger state.
            st_ = State::WaitUp;
            id0_ = (id == id0_) ? id1_ : id0_;
            return;
        }
        default:
            return;
    }
}

void TouchGestures::tick(uint32_t now) {
    if (st_ == State::Pending && now - t0_ >= cfg_.long_press_ms) st_ = State::Held;
}

bool TouchGestures::hold_feedback(uint32_t now, int& x, int& y, float& progress) const {
    if (st_ == State::Held) {
        x = x0_;
        y = y0_;
        progress = 1.0f;
        return true;
    }
    if (st_ != State::Pending || now - t0_ < cfg_.feedback_delay_ms) return false;
    x = x0_;
    y = y0_;
    const float span = static_cast<float>(cfg_.long_press_ms - cfg_.feedback_delay_ms);
    progress = span > 0.0f ? static_cast<float>(now - t0_ - cfg_.feedback_delay_ms) / span : 1.0f;
    if (progress > 1.0f) progress = 1.0f;
    return true;
}

}  // namespace ra2engine
