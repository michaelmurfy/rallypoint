// W3 asset-resolution census (developer tool; W3-FIX-A). Enumerates every
// object type the 26 RA2 campaign maps and the lobby maps place, plus the RA2
// buildable roster, and resolves each through (a) the clone's REAL resolver
// (ra2assets::AssetFS + art_base_names, the same calls engine/asset_search.inc
// makes) and (b) an "original-like" resolver (RA2 art Image= chain,
// Theater=/NewTheater= rules, global search over every loaded MIX). Prints one
// TSV line per (type, theater, context) and, on stderr, the share of PLACED
// objects that resolve plus every one that does not, with its cause.
// Build: ninja -C <build> art_census; run: art_census "D:/Red Alert 2/" > census.tsv
#include "ra2assets/mix.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/map.hpp"
#include "ra2assets/pkt.hpp"
#include "ra2assets/shp.hpp"
#include "ra2assets/asset_fs.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ra2assets;
static std::string D = "D:/Red Alert 2/";

static std::string up(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }
static std::string lo(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}
static std::vector<std::string> split(const std::string& v) {
    std::vector<std::string> out; std::stringstream ss(v); std::string t;
    while (std::getline(ss, t, ',')) { t = trim(t); if (!t.empty()) out.push_back(t); }
    return out;
}
static bool yes(const std::string& v) { std::string l = lo(trim(v)); return !l.empty() && (l[0] == 'y' || l[0] == 't' || l[0] == '1'); }

// ---------------------------------------------------------------- mix set
struct Mixes {
    std::map<std::string, std::unique_ptr<MixFile>> m;   // key "top/nested"
    MixFile* get(const std::string& key) { auto it = m.find(key); return it == m.end() ? nullptr : it->second.get(); }
    void add_top(const std::string& file) {
        try { m[lo(file)] = std::make_unique<MixFile>(MixFile::open_path(D + file)); } catch (...) {}
    }
    void add_nested(const std::string& top, const std::string& name) {
        MixFile* t = get(lo(top)); if (!t) return;
        try {
            if (!t->find(lo(name))) return;
            m[lo(top) + "/" + lo(name)] = std::make_unique<MixFile>(t->open_nested(lo(name)));
        } catch (...) {}
    }
};
static Mixes MX;
static Ini load_ini(const std::string& key, const std::string& name) {
    MixFile* m = MX.get(key); if (!m) return Ini::parse("");
    const MixEntry* e = m->find(lo(name)); if (!e) return Ini::parse("");
    auto r = m->read(*e); return Ini::parse(std::string(r.begin(), r.end()));
}
static bool has(const std::string& key, const std::string& file) { MixFile* m = MX.get(key); return m && m->find(lo(file)); }
static std::vector<std::string> where(const std::string& file) {
    std::vector<std::string> out;
    for (auto& [k, m] : MX.m) if (m && m->find(lo(file))) out.push_back(k);
    return out;
}
static std::string join(const std::vector<std::string>& v, const char* sep = ",") {
    std::string s; for (auto& x : v) { if (!s.empty()) s += sep; s += x; } return s;
}

// ---------------------------------------------------------------- theater
struct Th { const char* name; char letter; const char* iso; const char* art; const char* ext; bool yr; };
static const Th kTh[] = {{"TEMPERATE", 'T', "isotemp.mix", "temperat.mix", "tem", false},
                         {"SNOW", 'A', "isosnow.mix", "snow.mix", "sno", false},
                         {"URBAN", 'U', "isourb.mix", "urban.mix", "urb", false},
                         {"NEWURBAN", 'N', "isoubn.mix", "urbann.mix", "ubn", true},
                         {"DESERT", 'D', "isodes.mix", "desert.mix", "des", true},
                         {"LUNAR", 'L', "isolun.mix", "lunar.mix", "lun", true}};
static const Th* th_of(const std::string& n) { for (auto& t : kTh) if (up(n) == t.name) return &t; return nullptr; }
// engine art_chain(): YR theaters fall back to TEMPERATE.
static const Th* clone_chain(const Th* t) { return (t && !t->yr) ? t : &kTh[0]; }

// ---------------------------------------------------------------- data
static Ini RULESMD, ARTMD, RA2RULES, RA2ART;
static std::string rget(const Ini& i, const std::string& s, const std::string& k) { return trim(i.get(s, k)); }

// ---------------------------------------------------------------- truth
// Original resolver (building-render.md §1): art Image= (else section) + .shp;
// Theater=yes -> theater ext; NewTheater=yes + pattern -> letter munge, one 'G'
// retry; GLOBAL search over every loaded mix. We search the RA2 set for the
// theater plus the patch mixes; a YR-set hit is reported separately.
static std::vector<std::string> ra2_set(const Th* t) {
    std::vector<std::string> s = {"expand01.mix", "ecache01.mix", "ra2.mix/local.mix", "ra2.mix/cache.mix",
                                  "ra2.mix/conquer.mix", "ra2.mix/generic.mix", "ra2.mix/isogen.mix", "ra2.mix/neutral.mix"};
    if (t && !t->yr) { s.push_back(std::string("ra2.mix/") + t->art); s.push_back(std::string("ra2.mix/") + t->iso);
                       s.push_back(std::string("ra2.mix/") + t->ext + ".mix"); }
    return s;
}
static std::vector<std::string> md_set(const Th* t) {
    std::vector<std::string> s;
    for (auto& [k, m] : MX.m) if (k.rfind("ra2md.mix/", 0) == 0) s.push_back(k);
    (void)t; return s;
}
struct Truth { bool ok = false; std::string where, name; bool yr_only = false; };
static Truth truth_file(const std::vector<std::string>& names, const Th* t) {
    Truth r;
    for (auto& n : names) for (auto& k : ra2_set(t)) if (has(k, n)) { r.ok = true; r.where = k; r.name = n; return r; }
    for (auto& n : names) for (auto& k : md_set(t)) if (has(k, n)) { r.ok = true; r.yr_only = true; r.where = k; r.name = n; return r; }
    return r;
}
static std::vector<std::string> truth_names(const Ini& art, const std::string& sec, const Th* t, const char* dflt_ext = ".shp") {
    std::string img = rget(art, sec, "Image"); if (img.empty()) img = sec;
    std::string base = lo(img);
    std::string ext = dflt_ext;
    if (yes(rget(art, sec, "Theater")) && t) ext = std::string(".") + t->ext;
    std::vector<std::string> out;
    bool munge = yes(rget(art, sec, "NewTheater")) && base.size() >= 2 &&
                 std::string("gncy").find(base[0]) != std::string::npos && (base[1] == 'a' || base[1] == 't');
    if (munge && t) { std::string a = base; a[1] = (char)std::tolower(t->letter); out.push_back(a + ext); a[1] = 'g'; out.push_back(a + ext); }
    else out.push_back(base + ext);
    return out;
}

// ---------------------------------------------------------------- clone paths
struct Res { bool ok = false; std::string hit, tried; };
// W3-FIX-A: the clone's resolvers are no longer transcribed -- every loader
// goes through ra2assets::AssetFS (one global search list per theater) with
// the art_base_names() candidates, exactly as engine/asset_search.inc.
static std::map<std::string, std::unique_ptr<AssetFS>> FS;
static const AssetFS& fs_for(const Th* t) {
    const char* n = (t && !t->yr) ? t->name : "TEMPERATE";
    auto it = FS.find(n);
    if (it == FS.end()) it = FS.emplace(n, std::make_unique<AssetFS>(D, theater_chain(n))).first;
    return *it->second;
}
static const Th* CUR = &kTh[0];   // the row's theater (set by main)
static bool RA2ONLY = false;      // the playable roster resolves RA2 archives only
static Res fs_hit(const Th* t, const std::vector<std::string>& bases, const std::vector<std::string>& exts) {
    Res r;
    const AssetFS& fs = fs_for(t);
    for (auto& e : exts) for (auto& b : bases) {
        r.tried += (r.tried.empty() ? "" : "|") + b + e;
        std::string w;
        if (fs.find(b + e, nullptr, &w, RA2ONLY)) { r.ok = true; r.hit = w + ":" + b + e; return r; }
    }
    return r;
}
// load_unit_shp / campaign SHP vehicles: "<image>.shp" over the list.
static Res clone_infantry(const std::string& id) {
    return fs_hit(CUR, {lo(RULESMD.get(id, "Image", id))}, {".shp"});
}
// load_vehicle_assets: "<image>.vxl" + ".hva" (+ "<image>tur" when Turret=yes).
static Res clone_voxel(const std::string& id) {
    Res r; std::string img = lo(RULESMD.get(id, "Image", id));
    const AssetFS& fs = fs_for(CUR);
    auto part = [&](const std::string& b) { return fs.has(b + ".vxl") && fs.has(b + ".hva"); };
    r.tried = img + ".vxl/.hva";
    if (!part(img)) { r.hit = "hull missing"; return r; }
    if (yes(RULESMD.get(id, "Turret")) && !part(img + "tur")) { r.hit = "turret missing (" + img + "tur)"; return r; }
    r.ok = true; r.hit = fs.where(img + ".vxl") + ":" + img + ".vxl"; return r;
}
// Buildings (roster, campaign Types::building, depth_register_building): the
// map theater's letter, [themed, 'g'] + ".shp".
static Res clone_bld_chain(const std::string& id, const Th* chain) {
    std::string art_id = RULESMD.get(id, "Image", id);
    return fs_hit(chain, art_base_names(art_id, chain->letter), {".shp"});
}
static Res clone_bld_campaign(const std::string& id) { return clone_bld_chain(id, CUR && !CUR->yr ? CUR : &kTh[0]); }
// PresMap::art_shp: art Image= / rules Image= / name; bases + literal x [.ext, .shp].
static Res clone_presmap(const std::string& name, const Th* chain) {
    std::string image = trim(ARTMD.get(name, "Image")); if (image.empty()) image = trim(RULESMD.get(name, "Image")); if (image.empty()) image = name;
    auto bases = art_base_names(image, chain->letter);
    if (std::find(bases.begin(), bases.end(), lo(image)) == bases.end()) bases.push_back(lo(image));
    return fs_hit(chain, bases, {std::string(".") + chain->ext, ".shp"});
}
// PresFx::type: Theater=yes -> .ext first; else "<image>.shp".
static Res clone_anim(const std::string& name) {
    std::string image = trim(ARTMD.get(name, "Image")); if (image.empty()) image = name;
    if (yes(ARTMD.get(name, "Theater"))) {
        auto bases = art_base_names(image, CUR->letter);
        if (std::find(bases.begin(), bases.end(), lo(image)) == bases.end()) bases.push_back(lo(image));
        return fs_hit(CUR, bases, {std::string(".") + CUR->ext, ".shp"});
    }
    return fs_hit(CUR, {lo(image)}, {".shp"});
}
// PresFx::theater_type: type() then the NewTheater names .shp.
static Res clone_bld_anim(const std::string& name, const Th* chain) {
    Res r = clone_anim(name); if (r.ok) return r;
    std::string image = trim(ARTMD.get(up(name), "Image")); if (image.empty()) image = up(name);
    return fs_hit(chain, art_base_names(image, chain->letter), {".shp"});
}

// ---------------------------------------------------------------- usage
struct Use { std::set<std::string> maps; int count = 0; };
using Key = std::tuple<std::string /*kind*/, std::string /*id*/, std::string /*theater*/, std::string /*ctx*/>;
static std::map<Key, Use> USES;
static std::map<std::string, long> INST, INST_OK;
static std::vector<std::string> BAD;
static void use(const std::string& kind, const std::string& id, const std::string& th, const std::string& ctx, const std::string& map) {
    if (id.empty()) return;
    auto& u = USES[{kind, up(id), th, ctx}]; u.maps.insert(map); ++u.count;
}
static std::vector<std::string> fields(const std::string& v) {
    std::vector<std::string> f; std::string cur; for (char c : v) { if (c == ',') { f.push_back(trim(cur)); cur.clear(); } else cur += c; }
    f.push_back(trim(cur)); return f;
}
static std::vector<std::string> list_vals(const Ini& ini, const char* sec) {
    std::vector<std::string> o; for (auto& kv : ini.section(sec)) { auto v = trim(kv.second); if (!v.empty()) o.push_back(up(v)); } return o;
}
static void scan_map(const std::string& mixkey, const std::string& name, const std::string& ctx) {
    MixFile* m = MX.get(mixkey); if (!m) return;
    const MixEntry* e = m->find(lo(name)); if (!e) { std::cerr << "map missing " << name << "\n"; return; }
    auto raw = m->read(*e); std::string text(raw.begin(), raw.end());
    Ini ini = Ini::parse(text);
    std::string th = up(trim(ini.get("Map", "Theater")));
    for (auto& kv : ini.section("Structures")) { auto f = fields(kv.second); if (f.size() > 1) use("BUILDING", f[1], th, ctx, name); }
    for (auto& kv : ini.section("Units")) { auto f = fields(kv.second); if (f.size() > 1) use("VEHICLE", f[1], th, ctx, name); }
    for (auto& kv : ini.section("Infantry")) { auto f = fields(kv.second); if (f.size() > 1) use("INFANTRY", f[1], th, ctx, name); }
    for (auto& kv : ini.section("Aircraft")) { auto f = fields(kv.second); if (f.size() > 1) use("AIRCRAFT", f[1], th, ctx, name); }
    for (auto& kv : ini.section("Terrain")) use("TERRAIN", trim(kv.second), th, ctx, name);
    for (auto& kv : ini.section("Smudge")) { auto f = fields(kv.second); if (!f.empty()) use("SMUDGE", f[0], th, ctx, name); }
    // TaskForces members (campaign teams)
    for (auto& kv : ini.section("TaskForces")) {
        std::string tf = trim(kv.second);
        for (auto& m2 : ini.section(tf)) { if (m2.first == "NAME" || m2.first == "GROUP") continue; auto f = fields(m2.second); if (f.size() > 1) use("TEAMUNIT", f[1], th, ctx, name); }
    }
    // overlays: runtime index = order in [OverlayTypes] (rulesmd, as the clone)
    try {
        MapFile mf = MapFile::parse(text);
        if (mf.has_overlay()) {
            auto names = list_vals(RULESMD, "OverlayTypes");
            for (auto& c : mf.cells()) { int id = mf.overlay_at(c.x, c.y); if (id >= 0 && id != 255 && id < (int)names.size()) use("OVERLAY", names[id], th, ctx, name); }
        }
    } catch (const std::exception& ex) { std::cerr << "mapparse " << name << ": " << ex.what() << "\n"; }
}

int main(int argc, char** argv) {
    if (argc > 1) D = argv[1];
    for (const char* t : {"ra2.mix", "ra2md.mix", "expand01.mix", "ecache01.mix", "language.mix", "langmd.mix", "Maps01.mix", "MAPS02.MIX", "multimd.mix", "MULTI.MIX", "mapsmd03.mix"}) MX.add_top(t);
    for (const char* n : {"local.mix", "cache.mix", "conquer.mix", "generic.mix", "isogen.mix", "neutral.mix", "sidec01.mix", "sidec02.mix", "load.mix",
                          "temperat.mix", "isotemp.mix", "tem.mix", "snow.mix", "isosnow.mix", "sno.mix", "urban.mix", "isourb.mix", "urb.mix", "audio.mix", "cameo.mix"}) MX.add_nested("ra2.mix", n);
    for (const char* n : {"localmd.mix", "cachemd.mix", "conqmd.mix", "genermd.mix", "isogenmd.mix", "neutral.mix", "sidec02md.mix", "sidec01md.mix", "loadmd.mix",
                          "temperatmd.mix", "isotemmd.mix", "snowmd.mix", "isosnomd.mix", "urbanmd.mix", "isourbmd.mix", "urbann.mix", "isoubn.mix", "isoubnmd.mix", "urbannmd.mix",
                          "desert.mix", "isodes.mix", "isodesmd.mix", "desertmd.mix", "lunar.mix", "isolun.mix", "lunarmd.mix", "isolunmd.mix",
                          "expandmd01.mix", "expandmd02.mix", "expandmd03.mix", "expandmd04.mix", "expandmd05.mix", "expandmd99.mix"}) MX.add_nested("ra2md.mix", n);
    std::cerr << "mixes opened:"; for (auto& [k, m] : MX.m) std::cerr << " " << k << "(" << m->count() << ")"; std::cerr << "\n";

    // rulesmd/artmd exactly as load_ini_from_ra2md (top then nested chain)
    auto md_ini = [&](const char* n) {
        if (has("ra2md.mix", n)) return load_ini("ra2md.mix", n);
        for (const char* c : {"localmd.mix", "expandmd01.mix", "cachemd.mix", "conqmd.mix"}) if (has(std::string("ra2md.mix/") + c, n)) return load_ini(std::string("ra2md.mix/") + c, n);
        return Ini::parse("");
    };
    RULESMD = md_ini("rulesmd.ini"); ARTMD = md_ini("artmd.ini");
    RA2RULES = has("expand01.mix", "rules.ini") ? load_ini("expand01.mix", "rules.ini") : load_ini("ra2.mix/local.mix", "rules.ini");
    RA2ART = has("expand01.mix", "art.ini") ? load_ini("expand01.mix", "art.ini") : load_ini("ra2.mix/local.mix", "art.ini");
    std::cerr << "rulesmd sections=" << RULESMD.section_names().size() << " artmd=" << ARTMD.section_names().size()
              << " ra2rules=" << RA2RULES.section_names().size() << " ra2art=" << RA2ART.section_names().size()
              << " art.ini in expand01=" << has("expand01.mix", "art.ini") << " local=" << has("ra2.mix/local.mix", "art.ini") << "\n";

    // ---- maps
    // campaign: mission.ini section names (map file names) from ra2.mix/local.mix
    Ini mission = load_ini("ra2.mix/local.mix", "mission.ini");
    int ncamp = 0;
    for (auto& s : mission.section_names()) {
        std::string n = lo(s); if (n.size() < 5 || n.substr(n.size() - 4) != ".map") continue;
        std::string mix = up(n).rfind("SOV", 0) == 0 ? "maps02.mix" : "maps01.mix";
        if (!has(mix, n)) { if (has("maps01.mix", n)) mix = "maps01.mix"; else if (has("expand01.mix", n)) mix = "expand01.mix"; else { std::cerr << "campaign map not found " << n << "\n"; continue; } }
        scan_map(mix, n, "campaign"); ++ncamp;
    }
    // skirmish: the clone's lobby catalog (langmd.mix/missionsmd.pkt -> multimd.mix)
    int nsk = 0;
    if (MixFile* lm = MX.get("langmd.mix")) if (auto* pe = lm->find(std::string("missionsmd.pkt"))) {
        auto r = lm->read(*pe); PktFile p = PktFile::parse(std::string(r.begin(), r.end()));
        for (auto& e : p.entries()) { if (has("multimd.mix", e.file_name)) { scan_map("multimd.mix", e.file_name, "skirmish"); ++nsk; } }
    }
    std::cerr << "maps: campaign=" << ncamp << " skirmish=" << nsk << "\n";
    // buildable rosters (RA2 rules lists, TechLevel 1..10) in every RA2 theater, skirmish ctx
    for (auto* l : {"BuildingTypes", "InfantryTypes", "VehicleTypes", "AircraftTypes"}) {
        std::string kind = std::string(l) == "BuildingTypes" ? "BUILDING" : std::string(l) == "InfantryTypes" ? "INFANTRY" : std::string(l) == "VehicleTypes" ? "VEHICLE" : "AIRCRAFT";
        for (auto& id : list_vals(RA2RULES, l)) {
            int tl = std::atoi(RA2RULES.get(id, "TechLevel", "-1").c_str());
            if (tl < 1 || tl > 10) continue;
            for (const char* th : {"TEMPERATE", "SNOW", "URBAN"}) use(kind, id, th, "buildable", "(roster)");
        }
    }
    // ---- resolve
    auto inf_list = list_vals(RULESMD, "InfantryTypes"), veh_list = list_vals(RULESMD, "VehicleTypes"), air_list = list_vals(RULESMD, "AircraftTypes");
    auto in = [](const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    std::map<std::string, std::map<std::string, int>> tally;   // kind -> cause -> n
    std::map<std::string, int> total;
    std::set<std::string> anim_refs;
    auto add_anim = [&](const std::string& a, const std::string& why) { for (auto& x : split(a)) if (!x.empty() && lo(x) != "none") anim_refs.insert(up(x) + "|" + why); };
    std::printf("KIND\tID\tTHEATER\tCTX\tMAPS\tINSTANCES\tCLONE\tCAUSE\tDETAIL\n");
    for (auto& [k, u] : USES) {
        auto [kind, id, thn, ctx] = k;
        const Th* t = th_of(thn); const Th* ch = clone_chain(t); CUR = ch;
        std::string akind = kind;
        if (kind == "TEAMUNIT") akind = in(inf_list, id) ? "INFANTRY" : in(air_list, id) ? "AIRCRAFT" : "VEHICLE";
        Res r; std::string cause, detail;
        const bool rules_known = RULESMD.has(id);
        // anims referenced by this type
        {
            std::string img = RULESMD.get(id, "Image", id);
            add_anim(RULESMD.get(id, "Explosion"), id + ".Explosion");
            for (const char* wk : {"Primary", "Secondary", "ElitePrimary", "EliteSecondary"}) {
                std::string w = trim(RULESMD.get(id, wk)); if (w.empty()) continue;
                add_anim(RULESMD.get(w, "Anim"), w + ".Anim");
                std::string wh = trim(RULESMD.get(w, "Warhead")); if (!wh.empty()) add_anim(RULESMD.get(wh, "AnimList"), wh + ".AnimList");
            }
            for (const char* ak : {"ActiveAnim", "ActiveAnimTwo", "ActiveAnimThree", "ActiveAnimFour", "IdleAnim", "SpecialAnim", "SpecialAnimTwo", "SpecialAnimThree",
                                   "SuperAnim", "SuperAnimTwo", "SuperAnimThree", "SuperAnimFour", "DeployingAnim", "UnderDoorAnim", "RoofDeployingAnim", "UnderRoofDoorAnim",
                                   "ProductionAnim", "PreProductionAnim", "BibShape", "ActiveAnimDamaged", "ActiveAnimTwoDamaged", "IdleAnimDamaged", "SpecialAnimDamaged"})
                add_anim(ARTMD.get(img, ak), std::string("bldanim:") + img + "." + ak);
        }
        if (!rules_known && kind != "TERRAIN" && kind != "OVERLAY" && kind != "SMUDGE") { if (ctx == "buildable") continue; cause = "NO_RULES_SECTION"; }
        else if (akind == "INFANTRY") {
            r = clone_infantry(id);
        } else if (akind == "VEHICLE" || akind == "AIRCRAFT") {
            std::string img = up(RULESMD.get(id, "Image", id));
            bool shp_vehicle = !yes(ARTMD.get(img, "Voxel")) && akind == "VEHICLE";
            if (shp_vehicle) {
                r = clone_infantry(id);
            } else r = clone_voxel(id);
        } else if (akind == "BUILDING") {
            if (ctx == "campaign") r = clone_bld_campaign(id);
            else if (ctx == "buildable") { RA2ONLY = true; r = clone_bld_chain(id, ch); RA2ONLY = false; }
            else {
                // skirmish [Structures]: roster/garrisonable -> chain .shp; else PresMap civ dressing
                r = clone_bld_chain(id, ch);
                if (!r.ok) { std::string image = trim(RULESMD.get(id, "Image")); if (image.empty()) image = id; r = clone_presmap(up(image), ch); }
            }
        } else if (kind == "TERRAIN" || kind == "SMUDGE" || kind == "OVERLAY") {
            if (kind == "OVERLAY" && (yes(RULESMD.get(id, "Tiberium")) || id.rfind("DUMMY", 0) == 0)) continue;
            r = clone_presmap(id, ch);
        }
        // truth
        std::string art_sec = id;
        if (akind == "INFANTRY" || akind == "VEHICLE" || akind == "AIRCRAFT" || akind == "BUILDING") art_sec = up(RULESMD.get(id, "Image", id));
        if (kind == "OVERLAY" && trim(ARTMD.get(id, "Image")).empty() && !trim(RULESMD.get(id, "Image")).empty()) art_sec = up(trim(RULESMD.get(id, "Image")));
        bool vox = (akind == "VEHICLE" || akind == "AIRCRAFT") && (yes(ARTMD.get(art_sec, "Voxel")) || akind == "AIRCRAFT");
        const Ini& TART = (t && !t->yr) ? RA2ART : ARTMD;   // RA2 data for RA2 theaters
        std::vector<std::string> tn = truth_names(TART.has(art_sec) ? TART : ARTMD, art_sec, t, vox ? ".vxl" : ".shp");
        Truth tr = truth_file(tn, t);
        if (cause.empty()) {
            auto base = [](std::string h) { auto c = h.rfind(':'); return c == std::string::npos ? h : h.substr(c + 1); };
            if (r.ok && tr.ok && !vox && base(r.hit) != tr.name) cause = "WRONG_VARIANT(clone " + base(r.hit) + " vs original " + tr.name + ")";
            else if (r.ok && tr.ok && !tr.yr_only && r.hit.rfind("conqmd:", 0) == 0) cause = "YR_OVERRIDE(conqmd before conquer)";
            else if (r.ok) cause = "OK";
            else if (!tr.ok) cause = "ABSENT_EVERYWHERE";
            else if (tr.yr_only) cause = "ONLY_IN_RA2MD(" + tr.where + ")";
            else {
                // classify: present in RA2 set. Which rule did the clone miss?
                if (kind == "BUILDING" && ctx == "campaign" && t != &kTh[0] && clone_bld_chain(id, ch).ok) cause = "CAMPAIGN_TEMPERATE_HARDCODE";
                else if (t && t->yr) cause = "YR_THEATER_FALLBACK";
                else {
                    bool name_tried = false; std::string trn = tr.name;
                    for (auto& w : where(trn)) (void)w;
                    cause = "MIX_SCOPE_OR_NAME(found " + tr.where + ":" + tr.name + ")";
                    (void)name_tried;
                }
            }
        }
        if (!r.ok && r.hit.size()) detail = r.hit;
        detail += " truth=" + (tr.ok ? tr.where + ":" + tr.name : std::string("none")) + " tried=" + r.tried;
        std::string ckind = kind + "/" + ctx;
        if (ctx != "buildable") { INST[ctx] += u.count; if (cause == "OK" || cause.rfind("YR_OVERRIDE", 0) == 0) INST_OK[ctx] += u.count; else BAD.push_back(ctx + " " + kind + " " + id + " " + thn + " x" + std::to_string(u.count) + " " + cause); }
        ++total[ckind]; ++tally[ckind][cause.substr(0, cause.find('('))];
        std::string mlist; int mc = 0; for (auto& mm : u.maps) { if (mc++ < 6) mlist += mm + " "; } if (mc > 6) mlist += "+" + std::to_string(mc - 6);
        std::printf("%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\n", kind.c_str(), id.c_str(), thn.c_str(), ctx.c_str(), mlist.c_str(), u.count,
                    r.ok ? "ok" : "FAIL", cause.c_str(), detail.c_str());
    }
    // ---- anims referenced (clone PresFx::type / theater_type vs truth)
    std::map<std::string, std::string> anims; for (auto& s : anim_refs) { auto p = s.find('|'); anims.emplace(s.substr(0, p), s.substr(p + 1)); }
    for (auto& a : list_vals(RULESMD, "Animations")) anims.emplace(a, "[Animations]");
    for (auto& a : split(RULESMD.get("General", "DamageFireTypes"))) anims.emplace(up(a), "DamageFireTypes");
    for (auto& [a, why] : anims) {
        for (const char* thn : {"TEMPERATE", "SNOW", "URBAN"}) {
            const Th* t = th_of(thn); CUR = clone_chain(t);
            bool bld = why.rfind("bldanim:", 0) == 0;
            Res r = bld ? clone_bld_anim(a, t) : clone_anim(a);
            auto tn = truth_names(ARTMD, a, t);
            Truth tr = truth_file(tn, t);
            std::string cause = r.ok ? "OK" : !tr.ok ? "ABSENT_EVERYWHERE" : tr.yr_only ? "ONLY_IN_RA2MD" : "MIX_SCOPE_OR_NAME";
            std::string ck = std::string("ANIM/") + (bld ? "building" : why == "[Animations]" ? "listed" : "referenced");
            ++total[ck]; ++tally[ck][cause];
            if (!r.ok || (r.hit.rfind("conqmd:", 0) == 0 && tr.ok && !tr.yr_only))
                std::printf("ANIM\t%s\t%s\t%s\t%s\t0\t%s\t%s\t truth=%s:%s clonehit=%s\n", a.c_str(), thn, ck.c_str(), why.c_str(), r.ok ? "ok" : "FAIL",
                            r.ok ? "YR_OVERRIDE(conqmd first)" : cause.c_str(), tr.where.c_str(), tr.name.c_str(), r.hit.c_str());
            if (!bld && !yes(ARTMD.get(a, "Theater"))) break;   // theater-independent: one row
        }
    }
    std::fprintf(stderr, "\n==== PLACED OBJECTS (instances resolving / total) ====\n");
    for (auto& [c, n] : INST) std::fprintf(stderr, "%-10s %ld / %ld = %.2f%%\n", c.c_str(), INST_OK[c], n, n ? 100.0 * INST_OK[c] / n : 0.0);
    std::fprintf(stderr, "---- not resolving ----\n");
    for (auto& b : BAD) std::fprintf(stderr, "%s\n", b.c_str());
    std::fprintf(stderr, "\n==== SUMMARY (kind/ctx: total, cause counts) ====\n");
    for (auto& [k, n] : total) {
        std::fprintf(stderr, "%-24s total=%4d ", k.c_str(), n);
        for (auto& [c, m] : tally[k]) std::fprintf(stderr, " %s=%d", c.c_str(), m);
        std::fprintf(stderr, "\n");
    }
}
