// light_sources.inc -- W3-FIX-A: building light sources for PresLight.
// Included INSIDE the GameScene class body. RA2 BuildingClass::Unlimbo
// (FUN_004426d0) gives every building whose type has LightIntensity != 0 a
// LightSourceClass at its centre; it goes away with the building (Limbo).
// The light posts (IN*LMP, INGALITE, NEGLAMP) are InvisibleInGame buildings
// that exist only for this [RE: asset-search.md §3/§4].
struct LightType {
    bool has = false;
    PresLight::Src proto;
};
std::map<std::string, LightType> light_types_;

const LightType& light_type(const std::string& id) {
    auto it = light_types_.find(id);
    if (it != light_types_.end()) return it->second;
    auto num = [&](const char* k, double d) {
        const std::string v = pres_clean(rules_ini_.get(id, k));
        return v.empty() ? d : std::atof(v.c_str());
    };
    LightType t;
    // The keys are read as doubles and stored x1000 (FUN_00459a00 @0x45A553).
    const double inten = num("LightIntensity", 0.0);
    if (inten != 0.0) {
        t.has = true;
        t.proto.vis = static_cast<int>(num("LightVisibility", 5000.0));
        t.proto.intensity = static_cast<int>(inten * 1000.0);
        t.proto.r = static_cast<int>(num("LightRedTint", 1.0) * 1000.0);
        t.proto.g = static_cast<int>(num("LightGreenTint", 1.0) * 1000.0);
        t.proto.b = static_cast<int>(num("LightBlueTint", 1.0) * 1000.0);
    }
    return light_types_.emplace(id, t).first->second;
}

// Every live light-carrying building (world buildings + skirmish dressing).
void update_light_sources() {
    if (g_light != &light_) return;
    std::vector<PresLight::Src> v;
    for (const ra2engine::Building& b : world_.buildings()) {
        if (!b.alive || b.state == ra2engine::Building::State::Dead || b.type < 0 ||
            b.type >= static_cast<int>(ba_.roster.size()))
            continue;
        const LightType& lt = light_type(ba_.roster[static_cast<std::size_t>(b.type)].id);
        if (!lt.has) continue;
        PresLight::Src s = lt.proto;
        s.x = b.cx * 256.0 + b.fw * 128.0;
        s.y = b.cy * 256.0 + b.fh * 128.0;
        v.push_back(s);
    }
    for (const PresMap::LightObj& l : map_dressing_.lights()) {
        const LightType& lt = light_type(l.type);
        if (!lt.has) continue;
        PresLight::Src s = lt.proto;
        s.x = l.x * 256.0 + l.fw * 128.0;
        s.y = l.y * 256.0 + l.fh * 128.0;
        v.push_back(s);
    }
    light_.set_sources(std::move(v), a_.map);
}
