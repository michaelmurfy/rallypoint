// engine/net/command_bridge.cpp -- see command_bridge.hpp.
#include "net/command_bridge.hpp"

namespace ra2::net {

namespace {

using ra2engine::Command;
using ra2engine::Target;

TargetRef ref(const Target& t) {
    TargetRef r;
    if (t.none()) return r;
    r.rtti = static_cast<uint8_t>(t.rtti);
    r.id = static_cast<uint32_t>(t.id) & 0xFFFFFFu;
    return r;
}
Target tgt(const TargetRef& r) {
    Target t;
    if (r.rtti == 0) return t;
    t.rtti = static_cast<ra2engine::Rtti>(r.rtti);
    // 24-bit id; a cell keeps x | y << 16 -- sign-extend the y half's top byte.
    uint32_t id = r.id & 0xFFFFFFu;
    if (t.rtti == ra2engine::Rtti::Cell && (id & 0x800000u)) id |= 0xFF000000u;
    t.id = static_cast<int32_t>(id);
    return t;
}

constexpr int kChronoSphere = static_cast<int>(ra2engine::SuperweaponType::ChronoSphere);
constexpr int kChronoWarp = static_cast<int>(ra2engine::SuperweaponType::ChronoWarp);

NetCommand base(const Command& c) {
    NetCommand n;
    n.kind = static_cast<EventType>(static_cast<uint8_t>(c.type));
    n.house = c.house;
    n.frame = c.frame;
    n.mission = static_cast<uint8_t>(static_cast<int8_t>(c.mission));
    n.target = ref(c.target);
    n.dest = ref(c.dest);
    n.rtti = c.rtti_type;
    n.heap_id = c.heap_id;
    n.is_naval = c.is_naval;
    n.cell = CellXY{c.cx, c.cy};
    n.value = c.value;
    return n;
}

bool groups(EventType k) {
    return k == EventType::MEGAMISSION || k == EventType::IDLE || k == EventType::SCATTER || k == EventType::DEPLOY;
}
bool same_run(const NetCommand& a, const NetCommand& b) {
    return a.kind == b.kind && a.house == b.house && a.frame == b.frame && a.mission == b.mission &&
           a.target == b.target && a.dest == b.dest;
}

}  // namespace

std::vector<NetCommand> to_net(const Command& c) {
    std::vector<NetCommand> out;
    if (static_cast<int>(c.type) >= 0x2F) return out;   // clone-local extensions stay local
    NetCommand n = base(c);
    if (c.type == ra2engine::EventType::SpecialPlace) {
        n.value = c.value - 1;   // the original's binary SW index
        out.push_back(n);
        if (c.value == kChronoSphere && c.cx2 >= 0) {
            NetCommand w = n;
            w.value = kChronoWarp - 1;
            w.cell = CellXY{c.cx2, c.cy2};
            out.push_back(w);
        }
        return out;
    }
    if (c.whom.empty()) {
        out.push_back(n);
        return out;
    }
    for (const Target& t : c.whom) {   // one EventClass per Whom object (the codec re-groups runs)
        NetCommand e = n;
        e.whom = ref(t);
        out.push_back(e);
    }
    return out;
}

std::vector<Command> from_net(const std::vector<NetCommand>& recs) {
    std::vector<Command> out;
    uint32_t seq = 0;
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const NetCommand& n = recs[i];
        if (!is_sim_event(n.kind)) continue;
        Command c;
        c.type = static_cast<ra2engine::EventType>(static_cast<uint8_t>(n.kind));
        c.house = static_cast<int8_t>(n.house);
        c.frame = n.frame;
        c.seq = seq++;
        c.mission = static_cast<ra2engine::Mission>(static_cast<int8_t>(n.mission));
        c.target = tgt(n.target);
        c.dest = tgt(n.dest);
        c.rtti_type = n.rtti;
        c.heap_id = n.heap_id;
        c.is_naval = n.is_naval;
        c.cx = n.cell.x;
        c.cy = n.cell.y;
        c.value = n.value;
        if (n.kind == EventType::SPECIAL_PLACE) {
            c.value = n.value + 1;
            if (c.value == kChronoSphere && i + 1 < recs.size() && recs[i + 1].kind == EventType::SPECIAL_PLACE &&
                recs[i + 1].house == n.house && recs[i + 1].value + 1 == kChronoWarp) {
                c.cx2 = recs[i + 1].cell.x;
                c.cy2 = recs[i + 1].cell.y;
                ++i;
            }
            out.push_back(std::move(c));
            continue;
        }
        if (n.whom.rtti != 0) c.whom.push_back(tgt(n.whom));
        if (groups(n.kind))
            while (i + 1 < recs.size() && same_run(n, recs[i + 1]) && recs[i + 1].whom.rtti != 0) {
                c.whom.push_back(tgt(recs[i + 1].whom));
                ++i;
            }
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<Event> to_events(const ra2engine::Command& c) {
    std::vector<Event> out;
    for (const NetCommand& n : to_net(c)) {
        Event e;
        if (command_to_event(n, e)) out.push_back(e);
    }
    return out;
}

bool to_event(const ra2engine::Command& c, Event& out) {
    const std::vector<Event> v = to_events(c);
    if (v.size() != 1) return false;
    out = v[0];
    return true;
}

bool from_event(const Event& e, ra2engine::Command& out) {
    NetCommand n;
    if (!event_to_command(e, n)) return false;
    const std::vector<ra2engine::Command> v = from_net({n});
    if (v.size() != 1) return false;
    out = v[0];
    return true;
}

}  // namespace ra2::net
