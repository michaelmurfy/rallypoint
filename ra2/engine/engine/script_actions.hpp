#pragma once
// The ONE TeamClass ScriptType action table, shared by the AI-2 skirmish team
// interpreter (ai_team.cpp) and the P10 campaign map-team interpreter
// (campaign.cpp) [G-61, G-76].
//
// [RE: D:\ra2-re\p10out\c13.log:29-33 + c14.log; docs/re/campaign.md §7;
// docs/re/ai-team.md §3 (amended)] The executor reads the current line's
// (action, argument) pair and does `CMP EAX,0x40; JA <no-op>; JMP
// [EAX*4 + 0x6E94E4]` -- a 65-entry jump table, actions 0..64. Every anchor
// read in a handler agrees with the ModEnc / YRpp `TeamMissionType` numbering,
// so the names below are those. Anything > 64 is the executor's no-op.
//
// The earlier AI-2 table (8=AttackHouse, 9=MoveToWaypoint, 11=MoveToCell, ...)
// came from FUN_006e30d0, which is the map-trigger [Actions] argument
// classifier (called from FUN_006dc870, the P9 "actpre" helper), NOT the
// team-script executor -- retired here.
//
// SDL-free, World-free, header-only.

namespace ra2engine {

enum : int {
    kSaAttack = 0,               // attack quarry type <arg> (QuarryType below)       FUN_006ec5f0
    kSaAttackWaypoint = 1,       // attack waypoint <arg>                              FUN_006ebf00
    kSaBerserk = 2,              // handler is the epilogue itself (no-op, line never completes)
    kSaMoveWaypoint = 3,         // move to waypoint <arg>                             FUN_006ebd30
    kSaMoveCell = 4,             // move to cell x = arg % 128, y = arg / 128          (c14 idx 4)
    kSaGuard = 5,                // guard area for <arg> x 15 frames                   (c14 idx 5)
    kSaJump = 6,                 // jump to line <arg> (1-based: cursor = arg-2, then advanced)
    kSaWin = 7,                  // Flag_To_Win(Player)
    kSaUnload = 8,               // FUN_006ee670
    kSaDeploy = 9,               // FUN_006eca30
    kSaFollow = 10,              // follow friendlies                                  FUN_006ed0b0
    kSaDoMission = 11,           // assign mission <arg> (YRpp Mission enum)           FUN_006ecd40
    kSaSetGlobal = 12,           // PUSH 1 -> FUN_00689070
    kSaIdleAnim = 13,
    kSaLoad = 14,                // load onto transport                                FUN_006ec760
    kSaSpyOn = 15,
    kSaPatrol = 16,              // patrol to waypoint <arg>                           FUN_006ec240
    kSaChangeScript = 17,
    kSaChangeTeam = 18,
    kSaPanic = 19,
    kSaChangeHouse = 20,
    kSaScatter = 21,             // every member: vfunc+0x174 (scatter) -> line complete
    kSaShroudRun = 22,
    kSaLose = 23,                // Flag_To_Lose(Player)
    kSaPlaySpeech = 24,
    kSaDeleteMembers = 37,
    kSaClearGlobal = 38,
    kSaSetLocal = 39,
    kSaClearLocal = 40,
    kSaUnpanic = 41,
    kSaForceFacing = 42,
    kSaWaitLoaded = 43,
    kSaAttackEnemyBuilding = 46, // attack enemy building <arg = type | selector<<16>  FUN_006ed870
    kSaMoveEnemyBuilding = 47,   // move to enemy building <same encoding>             FUN_006ed950
    kSaScout = 48,
    kSaSuccess = 49,             // team+0x80 = 1 (line complete) AND team+0x84 = 1 (success);
                                 // the script CONTINUES -- it is not a disband
    kSaFlash = 50,
    kSaPlayAnim = 51,
    kSaTalkBubble = 52,
    kSaGatherEnemy = 53,         // gather at enemy base                               FUN_006eec60
    kSaGatherBase = 54,          // regroup at friendly base                           FUN_006eef70
    kSaIronCurtainMe = 55,
    kSaChronoPrepAB = 56,
    kSaChronoPrepAQ = 57,
    kSaMoveFriendlyBuilding = 58,// move to friendly building <same encoding as 46>    FUN_006edb20
    kSaAttackStructure = 59,     //                                                    FUN_006ebfd0
    // 60..64: for every member, if vfunc(+0x33c/+0x344/+0x340/+0x348/+0x34c) ->
    // remove it from the team (FUN_006e9dd0); one-shot, line complete.
    kSaRemoveIfA = 60, kSaRemoveIfB = 61, kSaRemoveIfC = 62, kSaRemoveIfD = 63,
    kSaRemoveIfE = 64,
    kSaMaxAction = 64,
};

// Guard-area duration unit [RE: c14.log idx 5 -- `LEA ECX,[ECX+ECX*2]; LEA
// ECX,[ECX+ECX*4]` stores arg*15 into the team's frame timer (+0x58/+0x60)].
constexpr int kScriptGuardFramesPerArg = 15;

// Jump target as a 0-based line index [RE: c14.log idx 6 -- `ADD ECX,-0x2`
// then the cursor is advanced past the jump line => line arg-1]. Stock
// aimd.ini confirms the 1-based argument (every guard loop ends `6,1`).
inline int script_jump_line(int arg) { return arg - 1; }

// Move-to-cell argument [RE: c14.log idx 4 -- x = arg % 128, y = arg / 128].
inline int script_cell_x(int arg) { return arg % 128; }
inline int script_cell_y(int arg) { return arg / 128; }

// Building-target argument of 46/47/58: low word = [BuildingTypes] index
// (0-based, rulesmd list order -- stock aimd.ini: 46,9 = NAPOWR, 47,7 = GAWEAP,
// 58,1 = GAREFN), high word = which instance [ModEnc: 0 least threat,
// 1 highest threat, 2 nearest, 3 farthest].
inline int script_building_type(int arg) { return arg & 0xffff; }
inline int script_building_selector(int arg) { return (arg >> 16) & 0xffff; }
enum : int { kBldSelLeastThreat = 0, kBldSelHighestThreat = 1, kBldSelNearest = 2,
             kBldSelFarthest = 3 };

// Attack quarry types (action 0's argument) [RE: YRpp QuarryType; stock
// aimd.ini: "General Harvester Hunt" 0,3, "General Factory Attack" 0,6,
// "General Base Defense Attack" 0,7, generic tails 0,2 then 0,1].
enum : int {
    kQuarryNone = 0, kQuarryAnything = 1, kQuarryBuildings = 2, kQuarryHarvesters = 3,
    kQuarryInfantry = 4, kQuarryVehicles = 5, kQuarryFactories = 6, kQuarryDefenses = 7,
    kQuarryThreats = 8, kQuarryPower = 9, kQuarryOccupiable = 10, kQuarryTech = 11,
};

// Mission numbers used by action 11 (YRpp Mission enum).
enum : int { kMissionSleep = 0, kMissionAttack = 1, kMissionGuard = 5, kMissionAreaGuard = 11,
             kMissionHunt = 15 };

}  // namespace ra2engine
