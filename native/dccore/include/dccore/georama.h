#pragma once
// The Georama rules of a town: which parts the player owns (Atla), the tips installed on the Atla board, completion, the villagers' requests, who lives
// where, and placing / removing parts with the copy limits. Ported from CEditPartsInfo, CEditGround::RequestCheck and the *Request functions, CEditGround
// (SetMapParts / DeleteMapParts wrappers) as described in notes/georama_edit.md. The geometry lives in dccore/edit.h; the state in SaveData.
#include <array>
#include <string>
#include <vector>
#include "dccore/elf.h"
#include "dccore/savedata.h"
#include "dccore/town.h"

namespace dccore {

struct GeoElemDef {
    int id = -1, rel = -1, npc = -1;
    std::string names[4];                 // frames switched by the element (town mode in names[0]; 1..3 for the interior door styles)
};

struct GeoPartDef {
    int max = 0, stackable = 0, plate = 0, name_msg = 0, comp_kind = 0;
    GeoElemDef elem[6];
};

struct GeoElemRow {
    int tile = 0, msg = 0, copies = 0;
};

struct GeoCharaRow {
    std::string model;
    int part = -1, element = -1, moves = 0, hide_until = 0;
    float speed = 0;
};

struct GeoAtraRow {
    int atra = -1, floor = 0, count = 0;
};

// The static tables of the main executable (EditPartsData, EditElementData, EditCharaData, AtraAppearData).
struct GeoTables {
    GeoPartDef parts[6][24];
    GeoElemRow elem[6][100];
    GeoCharaRow chara[5][20];
    std::vector<GeoAtraRow> atra[6];
    static GeoTables load(const Elf& elf);
};

// Dungeon pick-up of an Atla item (getAtraToSaveData): ids below 0x28 are parts of the town of that dungeon, the others are tips for its tray.
int georama_atra_parts_count(const SaveData& save, int town);   // _SGET_ATRA_PARTS_NUM: parts of the town that were found
void georama_atra_get(SaveData& save, const GeoTables& tables, int town, int atra_id);

class Georama {
public:
    Georama(Town& town, SaveData& save, const GeoTables& tables, int town_no);

    // ---- EditPartsInfo (saved)
    bool obtained(int p) const;
    int max_copies(int p) const;                 // the current limit (the saved value, the table value while it was never changed)
    int placed(int p) const;                     // copies on the map (a bridge or on-river part also counts the river piece under it)
    bool usable(int p) const { return obtained(p) && max_copies(p) >= 1; }   // CheckPartsInfo
    bool installed(int p, int k) const;
    void set_installed(int p, int k, bool on);
    bool complete(int p) const;                  // every element of the part is installed
    bool comp_event(int p) const;                // the completion event was played
    void set_comp_event(int p, bool on);
    int part_count() const;                      // rows of the table with a copy limit
    const GeoPartDef& def(int p) const { return tables_.parts[town_no_][p]; }
    int town_no() const { return town_no_; }

    // ---- editing
    // Places a copy at the cursor (CheckParts + copy limit); returns the slot or -1. `why` receives the reason.
    int place(int def, int rot, const float cursor[3], std::string* why = nullptr);
    // Takes away the part under the cursor; returns its part type (or -1) and rotation.
    int remove_at(const float cursor[3], int* rot_out = nullptr);
    // Writes the placement list to the save data (CEditGround::Save).
    void save_layout();
    // The placement list in the save data -> the town (EditLoad); the town must hold no placed parts yet.
    static std::vector<PlacedPart> saved_layout(const SaveData& save, int town_no);

    // ---- requests (the villagers' wishes about where things stand)
    void request_check();                        // RequestCheck: fills request(i) and the total
    // (the results live in the town's save block, GrdData[0..24]: houses read them while the player is indoors)
    bool request(int p) const;
    int request_total() const;
    int request_count() const;                   // how many are met
    bool all_requests_met() const { return request_total() > 0 && request_count() == request_total(); }
    int request_percent() const;                 // _SGET_REQUEST: met * 100 / total (100 when equal)
    bool request_event_flag() const;             // GrdData+0x64: the "all requests met" event was played
    void set_request_event_flag(bool on);

    // ---- villagers
    bool villager_present(int v) const;          // EdCheckVillager: the house stands, the element is installed, not hidden until its event
    int villager_part(int v) const;
    const GeoCharaRow* chara_row(int v) const { return (town_no_ > 4 || v < 0 || v >= 20) ? nullptr : &tables_.chara[town_no_][v]; }   // EditCharaData: moves, speed, house

    // ---- the Atla board
    bool tip_can_install(int p, int k, int chip) const;   // slot k of part p takes chip: empty, right id, prerequisite installed, villager gate
    bool install_tip(int p, int k, int chip);              // true = installed (the chip leaves the caller's hand)
    int tray_get(int slot) const;
    void tray_set(int slot, int chip);
    // Frames of a part's model that its elements show / hide (EdPartsObjectOnOff): frame name, visible.
    std::vector<std::pair<std::string, bool>> element_frames(int p) const;

    // ---- completion events: parts whose event should start (kind 2: the hero stands on the part)
    int pending_completion_under(const float hero[3]) const;

private:
    Town& town_;
    SaveData& save_;
    const GeoTables& tables_;
    int town_no_;
    std::array<bool, 24> requests_{};
    int request_total_ = 0;
    uint8_t* info(int p) const;
};

}  // namespace dccore
