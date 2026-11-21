#pragma once
// G-04: the original's keyboard command table. Pure C++17 (no SDL).
//
// [RE: gamemd fixB hunt] Every hotkey is a CommandClass whose GetName (vtable
// slot 1) returns the INI key name and whose Execute is vtable slot 8, e.g.
//   StopObject  vt@7EAA7C exec FUN_005368B0 -> FUN_0072FF70 (EVENT_IDLE)
//   GuardObject vt@7EAAA4 exec FUN_00536A30 -> FUN_0072FE30 (Area_Guard)
//   ScatterObject vt@7EAACC exec FUN_00536AB0 -> FUN_007300B0 (EVENT_SCATTER)
//   DeployObject vt@7EAA2C exec FUN_005369B0 -> FUN_0072FBD0
//   TypeSelect  vt@7EA964 exec FUN_005365E0 -> FUN_00731D70/FUN_00731D90
//   CenterBase  vt@7EAB1C exec FUN_00536BD0 -> FUN_00730670
//   StructureTab/DefenseTab/InfantryTab/UnitTab exec FUN_00535AC0/B80/C40/CD0
//   ToggleRepair/ToggleSell exec FUN_00536CD0/FUN_00536D50
//   Options     vt@7EAC5C exec FUN_00537000 -> FUN_00646AE0 (options dialog)
//   SidebarUp/Down exec FUN_00536EE0/F70 -> FUN_006A6090(+-1)
//   TeamCreate_N/TeamSelect_N/TeamAddSelect_N/TeamCenter_N (names built with
//   "%d") -> FUN_00730130/FUN_00730290/FUN_00730590/FUN_00730470.
// Bindings live in KeyboardMD.ini [Hotkey] <Name>=<code>, code = Windows VK
// + 256 (Shift) + 512 (Ctrl) + 1024 (Alt). The defaults below are the
// install's shipped KeyboardMD.ini verbatim.
#include <map>
#include <string>
#include <vector>

namespace ra2engine {

enum class HotCmd {
    None,
    Options, CenterView, CenterOnRadarEvent,
    TeamSelect, TeamAddSelect, TeamCreate, TeamCenter,   // arg = 1..10
    ToggleAlliance, PlaceBeacon, AllToCheer, DeployObject, Follow, GuardObject,
    CenterBase, ToggleRepair, ToggleSell, PreviousObject, NextObject,
    CombatantSelect, StopObject, TypeSelect, PageUser, ScatterObject,
    VeterancyNav, PlanningMode, Delete, ScreenCapture, HealthNav,
    StructureTab, DefenseTab, InfantryTab, UnitTab, SidebarUp, SidebarDown,
    View, SetView,   // arg = 1..4
    Taunt,           // arg = 1..8
};

struct HotBinding {
    HotCmd cmd = HotCmd::None;
    int arg = 0;
};

// W5-E [RE: the CommandClass vtables @0x7EA8F0.. -- slot 1 INI name, slot 2
// UI name, slot 3 category, slot 4 description, all CSF labels (strings
// @0x825FD8..0x826700); categories TXT_INTERFACE / TXT_SELECTION /
// TXT_CONTROL read from slot 3 of each command]: one row of the Keyboard
// page (0xA3) command list, in the binary's string-table order.
struct HotCommandInfo {
    std::string name;       // INI key ("StopObject", "TeamSelect_3")
    std::string ui_csf;     // "TXT_STOP_OBJECT"
    std::string cat_csf;    // "TXT_CONTROL"
    std::string desc_csf;   // "TXT_STOP_OBJECT_DESC"
    std::string fallback;   // English label when the CSF lacks it
    HotBinding binding;
};
const std::vector<HotCommandInfo>& hot_command_list();
// "Ctrl+Shift+F1" style label for a KeyboardMD.ini code (0 = "").
std::string hotkey_label(int code);

// Encode a key press the way KeyboardMD.ini does.
inline int hotkey_code(int vk, bool shift, bool ctrl, bool alt) {
    return vk + (shift ? 256 : 0) + (ctrl ? 512 : 0) + (alt ? 1024 : 0);
}

class HotkeyTable {
public:
    // The shipped KeyboardMD.ini [Hotkey] bindings.
    static HotkeyTable defaults();
    // Override bindings from a KeyboardMD.ini text ([Hotkey] Name=code lines;
    // unknown names are ignored, other sections skipped). Returns the number
    // of bindings applied.
    int load_ini_text(const std::string& text);
    HotBinding lookup(int vk, bool shift, bool ctrl, bool alt) const;
    // The code bound to a command (or -1).
    int code_of(HotCmd c, int arg = 0) const;
    // Name -> binding, e.g. "TeamSelect_3" -> {TeamSelect, 3}. False if unknown.
    static bool parse_name(const std::string& name, HotBinding& out);
    std::size_t size() const { return by_code_.size(); }
    // W5-E Keyboard page: assign `code` to `b` (a code already used by another
    // command is taken from it), the command a code is bound to, and the
    // [Hotkey] text the page writes (Keyboard.ini).
    void assign(const HotBinding& b, int code) { bind(b, code); }
    HotBinding command_at(int code) const {
        const auto it = by_code_.find(code);
        return it == by_code_.end() ? HotBinding{} : it->second;
    }
    std::string to_ini_text() const;

private:
    void bind(const HotBinding& b, int code);
    std::map<int, HotBinding> by_code_;
    std::map<std::pair<int, int>, int> code_by_cmd_;
};

}  // namespace ra2engine
