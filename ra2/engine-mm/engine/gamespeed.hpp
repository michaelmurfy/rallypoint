#pragma once
// Gap fix C / RE-T1 [RE: docs/re/gamespeed.md §2]: the original's logic frame
// rate per OptionsClass::GameSpeed in skirmish/campaign (FUN_0055cff0: one
// frame = GameSpeed ticks of a 60 Hz timer). The clone's rule time base is 15
// logic frames per engine second (GameSpeed 4), so a GameScene advances
// logic_fps/15 engine seconds per wall second.
namespace ra2engine {

// Logic frames per wall second for GameSpeed 0..6 (0 = fastest). GameSpeed 0
// is UNLIMITED in the original (timer rate 0); the clone caps it at 60
// [RE-FALLBACK: host-bound rate is not reproducible].
inline double logic_fps_for_game_speed(int game_speed) {
    if (game_speed <= 0) return 60.0;
    if (game_speed > 6) game_speed = 6;
    return 60.0 / game_speed;
}

// Engine seconds per wall second (the fixed-tick accumulator multiplier).
inline double game_speed_multiplier(int game_speed) {
    return logic_fps_for_game_speed(game_speed) / 15.0;
}

}  // namespace ra2engine
