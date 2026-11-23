#pragma once
// Yuri Slave Miner economy [rulesmd Enslaves= / gamemd SlaveManagerClass].
//
// RE (Steam YR gamemd.exe):
 //   TechnoType LoadFromINI **FUN_00714C0C** reads Slaved= / Enslaves= /
 //   SlaveRegenRate= / SlavesNumber= / SlaveReloadRate= onto TechnoType
 //   (+0xD3E / +0xD40 / +0xD48 / +0xD44 / +0xD4C).
 //   TechnoClass init **FUN_006F3FC0** (Enslaves push @0x6F400D): if
 //   TechnoType+0xD40 != null, `operator new(0x64)` + **FUN_006AF1A0**
 //   (SlaveManager ctor) stores the manager at Techno+0x2D8 — Building *or*
 //   Vehicle (SMIN). rulesmd puts Enslaves=SLAV on both [SMIN] and [YAREFN]
 //   with an EA note that the manager is brain-transplanted on deploy.
 //   AI **FUN_006AF5F0** (period 10) → **FUN_006AF6C0** (slave slots) /
 //   **FUN_006AFD60** (owner mission / scoot). Spawn/gather use the owner
 //   cell; dock math special-cases What_Am_I==Building (6) for the foundation.
 //   Scoot: ShortScan/LongScan/ScanCorrection/KickFrameDelay from Rules
 //   (FUN_0066D530 @0x6702D8..); pick nearer ore when improvement >
 //   ScanCorrection (FUN_006AFD60 @0x6B006D..0x6B01BF); KickFrameDelay
 //   wakes a Guard owner (FUN_006B1020). AI workforce target
 //   AISlaveMinerNumber (FUN_004FEACA @0x4FEACA vs Items at Rules+0x1340).
 //   Deploy transplant **FUN_006AF580** (SlaveManager::SetOwner): Unit→Building
 //   at FUN_007383E0 @0x739956 (after FUN_006B0D10 dock) and Building→Unit at
 //   Mission_Selling FUN_00449C30 @0x44A03C. Clears old Techno+0x2D8, installs
 //   on the new owner (destroys any manager the building already got from
 //   Enslaves=), retargets each SlaveControl's Techno+0x2DC. rulesmd: both
 //   [SMIN] and [YAREFN] list Enslaves=; "Brain transplant will check to make
 //   sure extra one is not created".
 //   ModEnc's "infantry leave BuildingTypes" means Enslaves= names an
 //   InfantryType, not that VehicleTypes cannot host a SlaveManager.
 //
 // Deployed YAREFN (Building) and undeployed SMIN (Unit master) keep
 // SlavesNumber workers. Slaves gather ore into Storage and deliver at the
 // master; income lands in house_credits. Pure: no SDL/OS. 15 logic fps.
#include <cstdint>
#include <string>
#include <vector>

#include "harvest.hpp"  // OreValues, kGulpSecs
#include "occupancy.hpp"
#include "ore.hpp"
#include "world.hpp"

namespace ra2engine {

// [General] SlaveMiner*Scan defaults from rulesmd (Yuri's Revenge).
constexpr int kSlaveMinerSlaveScanDefault = 14;
constexpr int kSlaveMinerShortScanDefault = 8;
constexpr int kSlaveMinerLongScanDefault = 48;
constexpr int kSlaveMinerScanCorrectionDefault = 3;
constexpr int kSlaveMinerKickFrameDelayDefault = 150;
constexpr int kSlaveLogicFps = 15;  // frames per engine second

struct SlaveMinerScootRules {
    int short_scan = kSlaveMinerShortScanDefault;
    int long_scan = kSlaveMinerLongScanDefault;
    int correction = kSlaveMinerScanCorrectionDefault;
    int kick_delay = kSlaveMinerKickFrameDelayDefault;
};

struct SlaveSpawn {
    int master = -1;           // Building or Unit index
    bool master_is_unit = false;
    int gx = 0, gy = 0;
    int house = 0;
    int storage_bales = 4;
};

struct SlaveMove {
    int unit = -1;
    int gx = 0, gy = 0;
};

// AI redeploy: move a master toward nearer ore (building = foundation origin;
// unit = destination cell beside the ore).
struct SlaveScoot {
    int master = -1;
    bool master_is_unit = false;
    int gx = 0, gy = 0;
};

// Drop cell beside a master's foundation (scan order); falls back to south
 // centre. Used for spawn and delivery approach.
bool slave_pad_cell(const Building& b, const std::vector<std::uint8_t>& passable, int gw, int gh,
                    const Occupancy& occ, int& out_x, int& out_y);

// Drop cell beside a unit master (8-ring then spiral).
bool slave_pad_cell_unit(const Unit& master, const std::vector<std::uint8_t>& passable, int gw,
                         int gh, const Occupancy& occ, int& out_x, int& out_y);

// Liberate every slave of a dead/missing master: clear is_slave / slave state.
 // Killer house: Building::killed_by / Unit::vet.killed_by → that unit's house
 // (YR SlaveManager free → killer's house). No killer stamp → house unchanged.
void liberate_orphaned_slaves(std::vector<Unit>& units, const std::vector<Building>& buildings);

// Nearest ore Chebyshev distance within `radius` (cells), or -1 if none.
 // Ties: smaller y, then smaller x. Pure helper for scoot tests.
int slave_miner_nearest_ore_dist(const OreField& ore, int cx, int cy, int radius, int& out_x,
                                 int& out_y);

// [RE: FUN_006AFD60 scoot]: true when a long-scan ore cell is at least
 // `correction` cells closer than the short-scan nearest (or when short finds
 // nothing and long does). `out_*` = cell the master should sit beside / on.
bool slave_miner_pick_scoot(const OreField& ore, int mx, int my, int short_scan, int long_scan,
                            int correction, int& out_x, int& out_y);

// Advance slave managers + slave gather AI by dt. Deterministic: ascending
 // building then unit-master then slave index. Emits Spawn/Move/Scoot requests.
 // `scoot` / `ai_mask` (per house, non-zero = computer): AI masters only; null
 // scoot disables redeploy. `guard_frames` on Building/Unit accumulate while
 // short-scan finds no ore (KickFrameDelay).
void step_slave_miners(std::vector<Unit>& units, std::vector<Building>& buildings,
                       const std::vector<BuildingTypeDef>& types, OreField& ore,
                       const std::vector<std::uint8_t>& passable, int gw, int gh, Occupancy& occ,
                       double dt, const OreValues& values, double* house_credits, int n_houses,
                       int slave_scan, const SlaveMinerScootRules* scoot, const std::uint8_t* ai_mask,
                       std::vector<SlaveSpawn>& spawns, std::vector<SlaveMove>& moves,
                       std::vector<SlaveScoot>& scoots);

// Install slave fields on a freshly spawned worker (tests / World).
inline void make_slave(Unit& u, int master, int storage_bales, bool master_is_unit = false) {
    u.is_slave = true;
    u.slave_master = master;
    u.slave_master_is_unit = master_is_unit;
    u.slave = SlaveState::SeekOre;
    u.storage_bales = storage_bales > 0 ? storage_bales : 4;
    u.bales_ore = 0.0;
    u.bales_gems = 0.0;
    u.harv_timer = kRescanSecs;  // seek on the next step
    u.is_harvester = false;
    u.manual_hold = false;
}

// Stamp Enslaves= master fields onto a Unit (SMIN) from its UnitTypeDef.
inline void arm_slave_master(Unit& u, int slaves_number, int regen, int reload, int storage) {
    if (slaves_number <= 0) return;
    u.is_slave_master = true;
    u.slaves_number = slaves_number;
    u.slave_regen_rate = regen;
    u.slave_reload_rate = reload;
    u.slave_worker_storage = storage > 0 ? storage : 4;
    u.slave_spawn_frames = 0;
    u.slave_needs_regen = false;
}

// [RE: FUN_006AF580]: move SlaveManager owner Unit→Building (SMIN deploy).
// Retargets live slaves; copies spawn/regen/guard timers. Clears the unit's
// master flags (unit may already be consume_unit'd). No-op if not a master.
void transplant_slave_manager_to_building(std::vector<Unit>& units, Building& building,
                                          int unit_master, int building_idx);

// [RE: FUN_006AF580]: Building→Unit (YAREFN undeploy). Arms the unit as master
// from `slaves_number` / rates / storage and the building's spawn timers.
void transplant_slave_manager_to_unit(std::vector<Unit>& units, Building& building,
                                      int building_idx, int unit_idx, int slaves_number,
                                      int regen, int reload, int storage);

}  // namespace ra2engine
