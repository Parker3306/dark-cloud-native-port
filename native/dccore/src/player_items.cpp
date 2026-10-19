// The three quick-use item slots (the "active items"): D-pad left/right picks the slot, the use button uses the item: medicines work at once, the water items
// need a drink animation (BattleActionDrink, mode 3). Ported from MoveChara / checkItemUsed / CheckStatusType / BattleActionDrink; throwing items is in player_throw.cpp.
#include "dccore/gamemath.h"
#include "dccore/status_ops.h"
#include "dccore/walk.h"

namespace dccore {

namespace {
// CActiveItemPack::CheckStatusType: 4 works at once, 2 is a drink with its own animation, 0 anything else (throwables, passives).
int status_type(int id) {
    switch (id) {
        case 0xaa: case 0x9b: case 0x9a: case 0x99: case 0x95: case 0x94: return 4;
        case 0x97: case 0x96: case 0x93: case 0x92: case 0x91: return 2;
        default: return 0;
    }
}
}  // namespace

// checkItemUsed: would the item in slot `slot` (0..2) do anything now?
bool Walker::quick_item_useful(int slot) const {
    const int ch = status->character();
    const int id = status->get<int16_t>(0x4362 + size_t(slot) * 2);
    if (id == -1) return false;
    const uint32_t err = status->get<uint32_t>(0x42c8 + size_t(ch) * 4);
    switch (id) {
        case 0xaa: case 0x9b: case 0x95: case 0x94:
            return status->life(ch) < status->max_life(ch);
        case 0x9a: return (err & 0x74) != 0;
        case 0x99: return (err & 0x40) != 0;
        case 0x97: return (err & 0x10) != 0;
        case 0x93: case 0x92: case 0x91: return gm::fadd(0.20000000298023224f, status->get<float>(0x4304 + size_t(ch) * 4)) < status->get<float>(0x42ec + size_t(ch) * 4);
        default: return true;
    }
}

void Walker::quick_item_consume(int slot) {
    const size_t id_at = 0x4362 + size_t(slot) * 2, count_at = 0x4368 + size_t(slot) * 2;
    if (status->get<int16_t>(count_at) == 1) {
        status->set<int16_t>(id_at, -1);
        status->set<int16_t>(count_at, 0);
    } else {
        status->set<int16_t>(count_at, int16_t(status->get<int16_t>(count_at) - 1));
    }
}

void Walker::quick_item_apply(int id) {
    if (!item_db) return;
    const UseEffect e = use_item_effect(*status, *item_db, id);
    if (e.poisoned) set_status_err(8);
}

// The part of MoveChara that reads the buttons.
void Walker::quick_items(const PadRaw& pad, bool frozen) {
    PlayerCombat& c = combat;
    const bool left = (pad.buttons & kPadItemLeft) != 0 && (item_prev_buttons & kPadItemLeft) == 0;
    const bool right = (pad.buttons & kPadItemRight) != 0 && (item_prev_buttons & kPadItemRight) == 0;
    const bool use = (pad.buttons & kPadUse) != 0 && (item_prev_buttons & kPadUse) == 0;
    item_prev_buttons = pad.buttons;
    if (c.mode == 0) {
        if (left) active_sel = active_sel < 2 ? 3 : active_sel - 1;
        if (right) active_sel = active_sel < 3 ? active_sel + 1 : 1;
    }
    if (!use || frozen || c.mode != 0 || c.can_act == 0) return;
    const int slot = active_sel - 1;
    const int id = status->get<int16_t>(0x4362 + size_t(slot) * 2);
    if (id == -1 || status->get<int16_t>(0x4368 + size_t(slot) * 2) <= 0) return;
    if (is_throw_item(id)) {
        throw_start(slot);
        return;
    }
    if (!quick_item_useful(slot)) return;
    const int type = status_type(id);
    if (type == 2) {   // a drink: the character holds it up and drinks (monsters wait)
        c.unit_hold = 1;
        c.mode = 3;
        c.can_act = 0;
        c.held = 1;
    } else if (type == 4) {
        quick_item_apply(id);
        quick_item_consume(slot);
    }
}

// BattleActionDrink
void Walker::drink_step() {
    PlayerCombat& c = combat;
    if (c.mode != 3) return;
    c.wish = 0x1c;
    const float t = motion.time;
    const float end = float(motion_table[0x1c].end);
    if (end - 1.0f <= t && t <= end) {
        const int slot = active_sel - 1;
        quick_item_apply(status->get<int16_t>(0x4362 + size_t(slot) * 2));
        quick_item_consume(slot);
        c.mode = 0;
        c.can_act = 1;
        c.no_interpolate = true;
        c.held = 0;
        c.wish = 0;
        c.unit_hold = 0;
    }
}

}  // namespace dccore
