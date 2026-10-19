#include "dccore/event_world.h"
#include <algorithm>
#include "dccore/gamemath.h"
#include "dccore/vu0.h"

namespace dccore {

bool has_item(const UserStatus& s, int id) {
    for (int i = 0; i < kBagSlots; ++i)
        if (s.get<int16_t>(0x436e + size_t(i) * 2) == id) return true;
    return false;
}

bool remove_item(UserStatus& s, int id) {
    for (int i = 0; i < kBagSlots; ++i) {
        const size_t at = 0x436e + size_t(i) * 2;
        if (s.get<int16_t>(at) == id) {
            s.set<int16_t>(at, -1);
            s.set<int16_t>(0x443c + size_t(i) * 2, 0);
            return true;
        }
    }
    return false;
}

int weapon_count(const UserStatus& s, const ItemDb& items, int weapon_id) {
    const uint8_t* w = items.weapon(weapon_id);
    if (!w || weapon_id < 0x101) return -1;
    const int owner = int(int8_t(w[10]));
    int n = 0;
    for (int i = 0; i < 10; ++i)
        if (0x100 < s.get<int16_t>(0x450c + size_t(owner) * 0xaa8 + size_t(i) * 0xf8)) ++n;
    return n;
}

int create_attach_volume(int item, GameRand& rng) {
    if (item < 0x5b || 0x5e < item) return 0;
    const int a = gm::scaled(50, rng.next());
    const int b = gm::scaled(50, rng.next());
    int v = 1;
    if (a + b < 0x1f) v = 2;
    if (a + b < 0x10) v = 3;
    return v;
}

int gate_key_for(int map_no, int variant) {
    constexpr int kKeys[7] = {0xc3, 0, 0xc9, 0xca, 0xcb, 0xcc, 0xce};
    if (map_no == 1) {
        constexpr int kMap1[3] = {196, 198, 205};
        return kMap1[std::clamp(variant, 0, 2)];
    }
    return kKeys[std::clamp(map_no, 0, 6)];
}

void EventWorld::init(FloorEvents events, std::vector<FloorMarker> floor_markers, FloorLayout* floor_layout, int map, int floor, int gate_key) {
    layout = floor_layout;
    ev = std::move(events);
    markers = std::move(floor_markers);
    marker_state.assign(markers.size(), MarkerState{});
    for (size_t i = 0; i < markers.size(); ++i) { marker_state[i].on = markers[i].marker.sw; marker_state[i].item_use = markers[i].marker.use_item; }
    map_no = map;
    floor_no = floor;
    gate_key_item = gate_key;
    if (layout) layout->door_open.assign(size_t(kGrid * kGrid), 0);
    notices.clear();
    atra_collected.clear();
    active_event = -1;
    exit_delay = 0;
}

int EventWorld::marker_index_at(const float pos[3]) const {
    for (size_t i = 0; i < markers.size(); ++i)
        if (marker_state[i].on && vu0::dist(markers[i].pos, pos) <= markers[i].marker.radius) return int(i);
    return -1;
}

void EventWorld::step(const float player_pos[3], bool ok_down, bool free, int character, UserStatus& status, const ItemDb& items, GameRand& rng) {
    const float pos4[4] = {player_pos[0], player_pos[1], player_pos[2], 1.0f};
    active_event = ev.active_event(pos4);
    if (ok_down && free && active_event != -1) {
        FloorEvent& e = ev.events[size_t(active_event)];
        if (e.type == 3) {   // atra statue
            AtraBall& a = ev.atras[size_t(e.index)];
            if (!a.active) {
                e.type = -1;
            } else if (character != 0) {
                notices.push_back({Notice::OnlyToan});
            } else {
                a.active = false;
                atra_collected.push_back(a.id);
                notices.push_back({Notice::AtraGot, a.id});
                e.type = -1;
            }
        } else if (e.type == 8) {   // a mimic: the chest was a monster
            Chest& c = ev.chests[size_t(e.index)];
            notices.push_back({Notice::Mimic, c.item});
            c.active = false;
            c.state = 1;
            e.type = -1;
        } else if (e.type == 2) {   // treasure chest
            Chest& c = ev.chests[size_t(e.index)];
            if (c.small) {
                const int r = check_item_get(status, c.item);
                if (r == 0) {
                    if (c.item == 0xe9) {
                        have_map = true;
                        notices.push_back({Notice::MapGot});
                    } else if (c.item == 0xea) {
                        have_crystal = true;
                        notices.push_back({Notice::CrystalGot});
                    } else {
                        const int volume = create_attach_volume(c.item, rng);
                        get_item(status, items, c.item, rng, volume);
                        notices.push_back({Notice::ItemGot, c.item, volume});
                    }
                    c.state = 1;
                    e.type = -1;
                } else {
                    notices.push_back({r == 1 ? Notice::BagFull : Notice::AttachFull, c.item});
                }
            } else if (c.quality != 0) {   // MoveChara: a big chest with a trap id runs the dungeon script (gameTask 400, DAT_01d567ec = the chest)
                notices.push_back({Notice::TrapChest, e.index, c.quality});
                if (c.quality == 5) e.type = -1;   // (the clown's chest can be asked once)
            } else {
                const int held = weapon_count(status, items, c.item);
                if (held < 10) {
                    get_item(status, items, c.item, rng);
                    notices.push_back({Notice::WeaponGot, c.item});
                    c.state = 1;
                    e.type = -1;
                } else {
                    notices.push_back({Notice::WeaponFull, c.item});
                }
            }
        }
    }
    for (TrapCircle& t : ev.traps) {   // DistTrapCircle / Run_TrapCircle
        if (t.state == 1 && vu0::dist(t.pos, pos4) <= 5.0f) {
            t.state = 2;
            const int ch = status.character();
            constexpr int16_t kFirstWeapon[6] = {257, 299, 314, 331, 347, 363};
            const int weapon = status.weapon(ch).id();
            int kind = t.kind;
            if (weapon == kFirstWeapon[std::clamp(ch, 0, 5)] || weapon == kFirstWeapon[std::clamp(ch, 0, 5)] + 1) kind = 0;
            notices.push_back({Notice::Trap, kind});
        }
        if (t.state == 2) {   // the circle fades (FUN_001c7ec8)
            t.timer += 1.0f;
            if (t.timer >= 60.0f) t.state = 0;
        }
    }
    if (exit_delay > 0 && --exit_delay == 0)
        for (size_t k = 0; k < markers.size(); ++k)
            if (markers[k].marker.event_id == 160) marker_state[k].on = true;
    // the trigger areas of the floor's parts
    for (size_t i = 0; i < markers.size(); ++i) {
        MarkerState& st = marker_state[i];
        const PartMarker& m = markers[i].marker;
        const bool inside = st.on && vu0::dist(markers[i].pos, player_pos) <= m.radius;
        const bool entered = inside && !st.inside;
        st.inside = inside;
        if (!entered && !(inside && ok_down)) continue;
        switch (m.event_id) {
            case 160:   // the exit stairs
                notices.push_back({Notice::ExitFloor});
                break;
            case 161:   // the gate: needs this dungeon's gate key
                if (has_item(status, gate_key_item)) {
                    remove_item(status, gate_key_item);
                    st.on = false;
                    exit_delay = 90;
                    if (layout) layout->door_open[size_t(markers[i].cell_y * kGrid + markers[i].cell_x)] |= 2;
                    notices.push_back({Notice::GateOpened, markers[i].cell_x, markers[i].cell_y});
                } else {
                    notices.push_back({Notice::NeedGateKey});
                }
                break;
            case 250:
            case 251: {   // a locked door: the door key is item 0xd8 + dungeon
                const int key = 0xd8 + map_no + (m.event_id - 250);
                if (has_item(status, key)) {
                    remove_item(status, key);
                    st.on = false;
                    if (layout) layout->door_open[size_t(markers[i].cell_y * kGrid + markers[i].cell_x)] |= 1;
                    notices.push_back({Notice::DoorOpened, markers[i].cell_x, markers[i].cell_y});
                } else {
                    notices.push_back({Notice::NeedDoorKey});
                }
                break;
            }
            default:
                break;
        }
    }
}

}  // namespace dccore
