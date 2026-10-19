#include "dccore/dungeon_script.h"
#include <memory>
#include "dccore/script.h"

namespace dccore {

FloorMapChoice choose_floor_map(const Bytes& event_stb, int floor, const std::function<int(int)>& game_int, const std::function<bool(int)>& game_flag) {
    FloorMapChoice choice;
    auto program = std::make_shared<ScriptProgram>(event_stb);
    ScriptVm vm;
    vm.load(program);
    vm.instruction_limit = 300000;
    vm.set_external([&](int32_t id, const ScriptArgs& a) {
        switch (id) {
            case 22:                                      // _GET_DUNGEON_FLOOR (dungeons 3, 4, 6 and 7 start with this instead of the entrance window)
            case 1010: a.set_int(0, floor); break;        // _GET_FLOOR_LEVEL
            case 1012: a.set_int(0, -1); break;           // _GET_OLD_FLOOR_LEVEL: first entry into the dungeon (the entrance window picks the floor)
            case 1051:                                    // _SET_DUNGEON_MAP(name, kind)
                choice.name = a.get_string(0);
                choice.kind = a.get_int(1);
                break;
            case 1068: choice.zone = a.get_int(0); break;                // _SET_LIMMIT_ZONE
            case 1080: choice.zone = -1; break;                          // _SET_RES_LIMMIT_ZONE
            case 712: if (a.n >= 2) a.set_int(1, game_int ? game_int(a.get_int(0)) : 0); break;       // _SGET_GAME_INT_FLAG
            case 701: if (a.n >= 2) a.set_int(1, game_flag && game_flag(a.get_int(0)) ? 1 : 0); break;  // _SGET_GAME_FLAG
            default: break;                               // everything else (windows, fades, flags...) is a no-op here
        }
        return true;
    });
    int state = vm.run(100);
    for (int i = 0; i < 64 && state == 1; ++i) state = vm.resume();
    return choice;
}

}  // namespace dccore
