#include "hotkeys.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ra2engine {

namespace {
struct Named { const char* name; HotCmd cmd; };
const Named kSimple[] = {
    {"Options", HotCmd::Options}, {"CenterView", HotCmd::CenterView},
    {"CenterOnRadarEvent", HotCmd::CenterOnRadarEvent}, {"ToggleAlliance", HotCmd::ToggleAlliance},
    {"PlaceBeacon", HotCmd::PlaceBeacon}, {"AllToCheer", HotCmd::AllToCheer},
    {"DeployObject", HotCmd::DeployObject}, {"Follow", HotCmd::Follow},
    {"GuardObject", HotCmd::GuardObject}, {"CenterBase", HotCmd::CenterBase},
    {"ToggleRepair", HotCmd::ToggleRepair}, {"ToggleSell", HotCmd::ToggleSell},
    {"PreviousObject", HotCmd::PreviousObject}, {"NextObject", HotCmd::NextObject},
    {"CombatantSelect", HotCmd::CombatantSelect}, {"StopObject", HotCmd::StopObject},
    {"TypeSelect", HotCmd::TypeSelect}, {"PageUser", HotCmd::PageUser},
    {"ScatterObject", HotCmd::ScatterObject}, {"VeterancyNav", HotCmd::VeterancyNav},
    {"PlanningMode", HotCmd::PlanningMode}, {"Delete", HotCmd::Delete},
    {"ScreenCapture", HotCmd::ScreenCapture}, {"StructureTab", HotCmd::StructureTab},
    {"DefenseTab", HotCmd::DefenseTab}, {"InfantryTab", HotCmd::InfantryTab},
    {"UnitTab", HotCmd::UnitTab}, {"SidebarUp", HotCmd::SidebarUp},
    {"SidebarDown", HotCmd::SidebarDown}, {"HealthNav", HotCmd::HealthNav},
};
struct Indexed { const char* prefix; HotCmd cmd; int lo, hi; };
const Indexed kIndexed[] = {
    {"TeamSelect_", HotCmd::TeamSelect, 1, 10}, {"TeamAddSelect_", HotCmd::TeamAddSelect, 1, 10},
    {"TeamCreate_", HotCmd::TeamCreate, 1, 10}, {"TeamCenter_", HotCmd::TeamCenter, 1, 10},
    {"SetView", HotCmd::SetView, 1, 4}, {"View", HotCmd::View, 1, 4},
    {"Taunt_", HotCmd::Taunt, 1, 8},
};

// D:\Red Alert 2\KeyboardMD.ini as shipped (the binary's defaults).
const char* kShipped =
    "[Hotkey]\n"
    "CenterView=12\nOptions=27\nCenterOnRadarEvent=32\n"
    "TeamSelect_10=48\nTeamSelect_1=49\nTeamSelect_2=50\nTeamSelect_3=51\nTeamSelect_4=52\n"
    "TeamSelect_5=53\nTeamSelect_6=54\nTeamSelect_7=55\nTeamSelect_8=56\nTeamSelect_9=57\n"
    "ToggleAlliance=65\nPlaceBeacon=66\nAllToCheer=67\nDeployObject=68\nInfantryTab=69\n"
    "Follow=70\nGuardObject=71\nCenterBase=72\nToggleRepair=75\nToggleSell=76\n"
    "PreviousObject=77\nNextObject=78\nCombatantSelect=80\nStructureTab=81\nUnitTab=82\n"
    "StopObject=83\nTypeSelect=84\nPageUser=85\nDefenseTab=87\nScatterObject=88\n"
    "VeterancyNav=89\nPlanningMode=90\nSidebarDown=98\nSidebarUp=104\nDelete=110\n"
    "View1=112\nView2=113\nView3=114\nView4=115\n"
    "Taunt_1=116\nTaunt_2=117\nTaunt_3=118\nTaunt_4=119\nTaunt_5=120\nTaunt_6=121\n"
    "Taunt_7=122\nTaunt_8=123\n"
    "TeamAddSelect_10=304\nTeamAddSelect_1=305\nTeamAddSelect_2=306\nTeamAddSelect_3=307\n"
    "TeamAddSelect_4=308\nTeamAddSelect_5=309\nTeamAddSelect_6=310\nTeamAddSelect_7=311\n"
    "TeamAddSelect_8=312\nTeamAddSelect_9=313\nScreenCapture=339\n"
    "TeamCreate_10=560\nTeamCreate_1=561\nTeamCreate_2=562\nTeamCreate_3=563\n"
    "TeamCreate_4=564\nTeamCreate_5=565\nTeamCreate_6=566\nTeamCreate_7=567\n"
    "TeamCreate_8=568\nTeamCreate_9=569\n"
    "SetView1=624\nSetView2=625\nSetView3=626\nSetView4=627\n"
    "TeamCenter_10=1072\nTeamCenter_1=1073\nTeamCenter_2=1074\nTeamCenter_3=1075\n"
    "TeamCenter_4=1076\nTeamCenter_5=1077\nTeamCenter_6=1078\nTeamCenter_7=1079\n"
    "TeamCenter_8=1080\nTeamCenter_9=1081\n";

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
}  // namespace

bool HotkeyTable::parse_name(const std::string& name, HotBinding& out) {
    for (const Named& n : kSimple)
        if (name == n.name) { out = {n.cmd, 0}; return true; }
    for (const Indexed& ix : kIndexed) {
        const std::string p = ix.prefix;
        if (name.size() <= p.size() || name.compare(0, p.size(), p) != 0) continue;
        const std::string num = name.substr(p.size());
        char* e = nullptr;
        const long v = std::strtol(num.c_str(), &e, 10);
        if (!e || *e != '\0' || v < ix.lo || v > ix.hi) continue;
        out = {ix.cmd, static_cast<int>(v)};
        return true;
    }
    return false;
}

void HotkeyTable::bind(const HotBinding& b, int code) {
    const auto key = std::make_pair(static_cast<int>(b.cmd), b.arg);
    const auto old = code_by_cmd_.find(key);
    if (old != code_by_cmd_.end()) by_code_.erase(old->second);   // rebinding moves it
    // W5-E: a code can drive one command only -- the previous owner loses it.
    if (code > 0) {
        const auto prev = by_code_.find(code);
        if (prev != by_code_.end() && !(prev->second.cmd == b.cmd && prev->second.arg == b.arg))
            code_by_cmd_[std::make_pair(static_cast<int>(prev->second.cmd), prev->second.arg)] = 0;
    }
    code_by_cmd_[key] = code;
    if (code > 0) by_code_[code] = b;
}

int HotkeyTable::load_ini_text(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    bool in_section = false;
    int applied = 0;
    while (std::getline(in, line)) {
        const std::size_t sc = line.find(';');
        if (sc != std::string::npos) line.erase(sc);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[') {
            in_section = (line == "[Hotkey]");
            continue;
        }
        if (!in_section) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        HotBinding b;
        if (!parse_name(trim(line.substr(0, eq)), b)) continue;
        char* e = nullptr;
        const std::string v = trim(line.substr(eq + 1));
        const long code = std::strtol(v.c_str(), &e, 10);
        if (!e || *e != '\0') continue;
        bind(b, static_cast<int>(code));
        ++applied;
    }
    return applied;
}

HotkeyTable HotkeyTable::defaults() {
    HotkeyTable t;
    t.load_ini_text(kShipped);
    return t;
}

HotBinding HotkeyTable::lookup(int vk, bool shift, bool ctrl, bool alt) const {
    const auto it = by_code_.find(hotkey_code(vk, shift, ctrl, alt));
    return it == by_code_.end() ? HotBinding{} : it->second;
}

int HotkeyTable::code_of(HotCmd c, int arg) const {
    const auto it = code_by_cmd_.find(std::make_pair(static_cast<int>(c), arg));
    return it == code_by_cmd_.end() ? -1 : it->second;
}

std::string HotkeyTable::to_ini_text() const {
    std::string out = "[Hotkey]\n";
    for (const HotCommandInfo& c : hot_command_list()) {
        const int code = code_of(c.binding.cmd, c.binding.arg);
        out += c.name + "=" + std::to_string(code < 0 ? 0 : code) + "\n";
    }
    return out;
}

const std::vector<HotCommandInfo>& hot_command_list() {
    static const std::vector<HotCommandInfo> list = [] {
        std::vector<HotCommandInfo> v;
        auto add = [&](const std::string& name, const std::string& ui, const std::string& cat,
                       const std::string& desc, const std::string& fb) {
            HotCommandInfo c{name, ui, cat, desc, fb, {}};
            HotkeyTable::parse_name(name, c.binding);
            v.push_back(c);
        };
        const char* I = "TXT_INTERFACE";
        const char* S = "TXT_SELECTION";
        const char* K = "TXT_CONTROL";
        add("StructureTab", "TXT_STRUCTURE_TAB", I, "TXT_STRUCTURE_TAB_DESC", "Structures Tab");
        add("DefenseTab", "TXT_DEFENSE_TAB", I, "TXT_DEFENSE_TAB_DESC", "Defenses Tab");
        add("InfantryTab", "TXT_INFANTRY_TAB", I, "TXT_INFANTRY_TAB_DESC", "Infantry Tab");
        add("UnitTab", "TXT_UNIT_TAB", I, "TXT_UNIT_TAB_DESC", "Units Tab");
        // [RE-FALLBACK: the team / taunt commands' category label was not read
        // from their shared %d-formatted vtables; TXT_TEAM / TXT_TAUNT.]
        for (int i = 1; i <= 10; ++i)
            add("TeamSelect_" + std::to_string(i), "TXT_SELECT_TEAM", "TXT_TEAM", "TXT_SELECT_TEAM_DESC",
                "Select Team " + std::to_string(i));
        for (int i = 1; i <= 10; ++i)
            add("TeamAddSelect_" + std::to_string(i), "TXT_ADD_SELECT_TEAM", "TXT_TEAM", "TXT_ADD_SELECT_TEAM_DESC",
                "Add To Team " + std::to_string(i));
        for (int i = 1; i <= 10; ++i)
            add("TeamCenter_" + std::to_string(i), "TXT_CENTER_TEAM", "TXT_TEAM", "TXT_CENTER_TEAM_DESC",
                "Center Team " + std::to_string(i));
        for (int i = 1; i <= 10; ++i)
            add("TeamCreate_" + std::to_string(i), "TXT_CREATE_TEAM", "TXT_TEAM", "TXT_CREATE_TEAM_DESC",
                "Create Team " + std::to_string(i));
        for (int i = 1; i <= 8; ++i)
            add("Taunt_" + std::to_string(i), "TXT_TAUNT_NUMBER", "TXT_TAUNT", "TXT_TAUNT_DESC",
                "Taunt " + std::to_string(i));
        add("NextObject", "TXT_NEXT_OBJECT", S, "TXT_NEXT_OBJECT_DESC", "Next Unit");
        add("PlanningMode", "Cmnd:PlanningMode", K, "Cmnd:PlanningModeDesc", "Planning Mode");
        add("CombatantSelect", "Cmnd:CombatantSelect", S, "Cmnd:CombatantSelectDesc", "Select Combatants");
        add("TypeSelect", "Cmnd:TypeSelect", S, "Cmnd:TypeSelectDesc", "Select Same Type");
        add("HealthNav", "Cmnd:HealthNavigation", S, "Cmnd:HealthNavigationDesc", "Health Navigation");
        add("VeterancyNav", "Cmnd:VetNavigation", S, "Cmnd:VetNavigationDesc", "Veterancy Navigation");
        add("PreviousObject", "TXT_PREV_OBJECT", S, "TXT_PREV_OBJECT_DESC", "Previous Unit");
        add("StopObject", "TXT_STOP_OBJECT", K, "TXT_STOP_OBJECT_DESC", "Stop");
        add("AllToCheer", "Cmnd:AllCheer", K, "Cmnd:AllCheerDesc", "All To Cheer");
        add("DeployObject", "TXT_DEPLOY_OBJECT", K, "TXT_DEPLOY_OBJECT_DESC", "Deploy");
        add("GuardObject", "TXT_GUARD", K, "TXT_GUARD_DESC", "Guard");
        add("ScatterObject", "TXT_SCATTER", K, "TXT_SCATTER_DESC", "Scatter");
        add("CenterView", "TXT_CENTER_VIEW", S, "TXT_CENTER_VIEW_DESC", "Center View");
        add("CenterBase", "TXT_CENTER_BASE", S, "TXT_CENTER_BASE_DESC", "Center Base");
        add("ToggleAlliance", "TXT_ALLIANCE", K, "TXT_ALLIANCE_DESC", "Alliance");
        add("ToggleRepair", "TXT_REPAIR_MODE", I, "TXT_REPAIR_MODE_DESC", "Repair Mode");
        add("ToggleSell", "TXT_SELL_MODE", I, "TXT_SELL_MODE_DESC", "Sell Mode");
        add("PlaceBeacon", "TXT_PLACE_BEACON", I, "TXT_PLACE_BEACON_DESC", "Place Beacon");
        add("CenterOnRadarEvent", "TXT_RADAR_EVENT", I, "TXT_RADAR_EVENT_DESC", "Center On Radar Event");
        add("SidebarUp", "TXT_SIDEBAR_UP", I, "TXT_SIDEBAR_UP_DESC", "Sidebar Up");
        add("SidebarDown", "TXT_SIDEBAR_DOWN", I, "TXT_SIDEBAR_DOWN_DESC", "Sidebar Down");
        add("Options", "TXT_OPTIONS", I, "TXT_OPTIONS_DESC", "Options");
        for (int i = 1; i <= 4; ++i)
            add("View" + std::to_string(i), "TXT_VIEW_BOOKMARK" + std::to_string(i), I,
                "TXT_VIEW_BOOKMARK" + std::to_string(i) + "_DESC", "View Bookmark " + std::to_string(i));
        for (int i = 1; i <= 4; ++i)
            add("SetView" + std::to_string(i), "TXT_SET_BOOKMARK" + std::to_string(i), I,
                "TXT_SET_BOOKMARK" + std::to_string(i) + "_DESC", "Set Bookmark " + std::to_string(i));
        add("Follow", "TXT_FOLLOW", I, "TXT_FOLLOW_DESC", "Follow");
        add("ScreenCapture", "TXT_SCRNCAP", I, "TXT_SCRNCAP_DESC", "Screen Capture");
        add("PageUser", "TXT_PAGEUSER", I, "TXT_PAGEUSER_DESC", "Page User");
        add("Delete", "Cmnd:Delete", I, "Cmnd:DeleteDesc", "Delete");
        return v;
    }();
    return list;
}

std::string hotkey_label(int code) {
    if (code <= 0) return std::string();
    std::string s;
    if (code & 512) s += "Ctrl+";
    if (code & 1024) s += "Alt+";
    if (code & 256) s += "Shift+";
    const int vk = code & 255;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        s += static_cast<char>(vk);
    } else if (vk >= 112 && vk <= 123) {
        s += "F" + std::to_string(vk - 111);
    } else if (vk >= 96 && vk <= 105) {
        s += "Num" + std::to_string(vk - 96);
    } else {
        switch (vk) {
            case 8: s += "Backspace"; break;
            case 9: s += "Tab"; break;
            case 12: s += "Clear"; break;
            case 13: s += "Enter"; break;
            case 27: s += "Esc"; break;
            case 32: s += "Space"; break;
            case 33: s += "PgUp"; break;
            case 34: s += "PgDn"; break;
            case 35: s += "End"; break;
            case 36: s += "Home"; break;
            case 37: s += "Left"; break;
            case 38: s += "Up"; break;
            case 39: s += "Right"; break;
            case 40: s += "Down"; break;
            case 45: s += "Insert"; break;
            case 46: s += "Delete"; break;
            default: s += "#" + std::to_string(vk); break;
        }
    }
    return s;
}

}  // namespace ra2engine
