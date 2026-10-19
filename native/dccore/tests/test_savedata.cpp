#include <cstdio>
#include "dccore/savedata.h"
using namespace dccore;
int main() {
    int bad = 0;
    auto chk = [&](bool c, const char* m) { if (!c) { std::printf("FAIL %s\n", m); ++bad; } };
    SaveData s;
    chk(s.get<int32_t>(0x1cc) == -1, "init -1");
    chk(s.parts_count(0) == 0, "no parts");
    s.set_game_flag(37, true); chk(s.game_flag(37) && !s.game_flag(36), "flag");
    s.set_game_flag(37, false); chk(!s.game_flag(37), "flag off");
    s.set_map_flag(3, 40, true); chk(s.map_flag(3, 40) && !s.map_flag(3, 41) && !s.map_flag(4, 40), "map flag");
    chk(!s.map_flag(80, 0) && !s.map_flag(0, 512), "bounds");
    s.set_now_time(8.0f); s.add_now_time(1.0f); chk(s.day() == 1, "dawn advances day");
    s.set_now_time(11.5f); s.add_now_time(1.0f); chk(s.now_time() == 0.5f, "wrap");
    s.add_fishing_points(20000); chk(s.fishing_points() == 9999, "fish cap");
    s.set_day(99999); chk(s.day() == 0x270e, "day cap");
    UserStatus u = UserStatus::new_game(), v;
    s.store_status(u); s.load_status(v);
    chk(std::memcmp(u.raw.data(), v.raw.data(), SaveData::kStatusSize) == 0, "status roundtrip");
    std::printf(bad ? "FAILED\n" : "savedata ok\n");
    return bad;
}
