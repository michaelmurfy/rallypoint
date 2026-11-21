// engine/net/event_map.cpp -- see event_map.hpp.
#include "net/event_map.hpp"

namespace ra2::net {

bool is_sim_event(EventType t) {
    switch (t) {
        case EventType::FRAMESYNC:
        case EventType::FRAMEINFO:
        case EventType::TIMING:
        case EventType::RESPONSE_TIME:
        case EventType::PROCESS_TIME:
        case EventType::LATENCYFUDGE:
        case EventType::RESPONSE_TIME2:
        case EventType::MEGAFRAMEINFO:
        case EventType::PACKETTIMING:
        case EventType::ABOUTTOEXIT:
        case EventType::FALLBACKHOST:
        case EventType::ADDRESSCHANGE:
        case EventType::MESSAGE:
            return false;
        default:
            return static_cast<uint8_t>(t) < kVanillaEventTypes;
    }
}

bool command_to_event(const NetCommand& c, Event& out) {
    const int h = c.house;
    switch (c.kind) {
        case EventType::EMPTY:
        case EventType::DESTRUCT:
        case EventType::OPTIONS:
        case EventType::EXIT:
        case EventType::SAVEGAME:
        case EventType::PAGEUSER:
        case EventType::PLANCOMMIT:
            out = ev::make(c.kind, h);
            break;
        case EventType::POWERON:
        case EventType::POWEROFF:
        case EventType::IDLE:
        case EventType::SCATTER:
        case EventType::DEPLOY:
        case EventType::DETONATE:
        case EventType::PRIMARY:
        case EventType::REPAIR:
        case EventType::SELL:
        case EventType::PLANNODEDELETE:
            out = ev::whom(c.kind, h, c.whom);
            break;
        case EventType::ALLY:
        case EventType::GAMESPEED:
        case EventType::SPECIAL:
        case EventType::REMOVEPLAYER:
        case EventType::ALLCHEER:
            out = ev::int_event(c.kind, h, c.value);
            break;
        case EventType::MEGAMISSION: {
            ev::MegaMission m;
            m.whom = c.whom;
            m.mission = c.mission;
            m.target = c.target;
            m.dest = c.dest;
            m.follow = c.follow;
            m.planning = c.planning;
            out = ev::mega_mission(h, m);
            break;
        }
        case EventType::MEGAMISSION_F: {
            ev::MegaMissionF m;
            m.whom = c.whom;
            m.mission = c.mission;
            m.target = c.target;
            m.dest = c.dest;
            m.speed = c.speed;
            m.max_speed = c.max_speed;
            out = ev::mega_mission_f(h, m);
            break;
        }
        case EventType::PRODUCE:
        case EventType::SUSPEND:
        case EventType::ABANDON:
        case EventType::ABANDON_ALL:
            out = ev::production(c.kind, h, ev::Production{c.rtti, c.heap_id, c.is_naval});
            break;
        case EventType::PLACE: {
            ev::Place p;
            p.rtti = c.rtti;
            p.heap_id = c.heap_id;
            p.is_naval = c.is_naval;
            p.cell = c.cell;
            out = ev::place(h, p);
            break;
        }
        case EventType::SPECIAL_PLACE: {
            ev::SpecialPlace p;
            p.sw_index = c.value;
            p.cell = c.cell;
            out = ev::special_place(h, p);
            break;
        }
        case EventType::ANIMATION:
            out = ev::animation(h, ev::Animation{c.value, c.rtti, c.x, c.y});
            break;
        case EventType::SELLCELL:
            out = ev::sell_cell(h, c.cell);
            break;
        case EventType::ARCHIVE:
        case EventType::PLANCONNECT:
            out = ev::two_targets(c.kind, h, c.whom, c.target);
            break;
        case EventType::ADDPLAYER:
            out = ev::add_player(h, c.blob);
            break;
        default:
            return false;
    }
    out.frame = c.frame;
    return true;
}

bool event_to_command(const Event& e, NetCommand& out) {
    if (!is_sim_event(e.kind())) return false;
    NetCommand c;
    c.kind = e.kind();
    c.house = e.house;
    c.frame = e.frame;
    switch (e.kind()) {
        case EventType::POWERON:
        case EventType::POWEROFF:
        case EventType::IDLE:
        case EventType::SCATTER:
        case EventType::DEPLOY:
        case EventType::DETONATE:
        case EventType::PRIMARY:
        case EventType::REPAIR:
        case EventType::SELL:
        case EventType::PLANNODEDELETE:
            c.whom = e.target(0);
            break;
        case EventType::ALLY:
        case EventType::GAMESPEED:
        case EventType::SPECIAL:
        case EventType::REMOVEPLAYER:
        case EventType::ALLCHEER:
            c.value = e.i32(0);
            break;
        case EventType::MEGAMISSION: {
            const ev::MegaMission m = ev::get_mega_mission(e);
            c.whom = m.whom;
            c.mission = m.mission;
            c.target = m.target;
            c.dest = m.dest;
            c.follow = m.follow;
            c.planning = m.planning;
            break;
        }
        case EventType::MEGAMISSION_F: {
            const ev::MegaMissionF m = ev::get_mega_mission_f(e);
            c.whom = m.whom;
            c.mission = m.mission;
            c.target = m.target;
            c.dest = m.dest;
            c.speed = m.speed;
            c.max_speed = m.max_speed;
            break;
        }
        case EventType::PRODUCE:
        case EventType::SUSPEND:
        case EventType::ABANDON:
        case EventType::ABANDON_ALL: {
            const ev::Production p = ev::get_production(e);
            c.rtti = p.rtti;
            c.heap_id = p.heap_id;
            c.is_naval = p.is_naval;
            break;
        }
        case EventType::PLACE: {
            const ev::Place p = ev::get_place(e);
            c.rtti = p.rtti;
            c.heap_id = p.heap_id;
            c.is_naval = p.is_naval;
            c.cell = p.cell;
            break;
        }
        case EventType::SPECIAL_PLACE: {
            const ev::SpecialPlace p = ev::get_special_place(e);
            c.value = p.sw_index;
            c.cell = p.cell;
            break;
        }
        case EventType::ANIMATION: {
            const ev::Animation a = ev::get_animation(e);
            c.value = a.anim_id;
            c.rtti = a.house_id;
            c.x = a.x;
            c.y = a.y;
            break;
        }
        case EventType::SELLCELL:
            c.cell = e.cell(0);
            break;
        case EventType::ARCHIVE:
        case EventType::PLANCONNECT:
            c.whom = e.target(0);
            c.target = e.target(5);
            break;
        case EventType::ADDPLAYER:
            c.blob = e.blob;
            break;
        default:
            break;
    }
    out = std::move(c);
    return true;
}

}  // namespace ra2::net
