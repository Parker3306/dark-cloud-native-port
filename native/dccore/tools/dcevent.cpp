// dcevent <iso> <map> <function> [frames] [pad]: runs one function of a map's event.stb headless and prints the external calls, the window text and the state.
// pad: a script bit mask that is held down in every frame (0x20 = OK) to click through the messages.
#include <cstdio>
#include <cstdlib>
#include "dccore/disc.h"
#include "dccore/event_runtime.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: dcevent <iso> <map> <function> [frames] [pad]\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const std::string map = argv[2];
        const int fid = std::atoi(argv[3]);
        const int frames = argc > 4 ? std::atoi(argv[4]) : 600;
        const uint32_t pad = argc > 5 ? uint32_t(std::strtoul(argv[5], nullptr, 0)) : 0;
        const std::string dir = "gedit/" + map + "/";
        auto prog = std::make_shared<ScriptProgram>(disc.read(dir + "event.stb"));
        struct Host : EventHost {
            void unknown_ext(int id, const char* name) override { std::printf("  (unimplemented %d %s)\n", id, name); }
            void sound(const char* op, int a, int b, const float*) override { std::printf("  sound %s %d %d\n", op, a, b); }
        } host;
        EventRuntime rt(prog, &host);
        SaveData save;
        UserStatus status = UserStatus::new_game();
        GameRand rng;
        rng.seed(1);
        rt.save = &save;
        rt.status = &status;
        rt.rng = &rng;
        rt.map_dir = dir;
        rt.system_bank = MessageBank::parse(disc.read("meswin/_systeme.bin"));
        if (disc.contains(dir + map + "_1.mes")) { rt.event_bank = MessageBank::parse(disc.read(dir + map + "_1.mes")); rt.has_event_bank = true; }
        if (disc.contains(dir + map + "talk_1.mes")) { rt.talk_bank = MessageBank::parse(disc.read(dir + map + "talk_1.mes")); rt.has_talk_bank = true; }
        rt.trace = [](int id, const char* name, const ScriptArgs& a) {
            std::printf("%s(", name);
            for (int i = 0; i < a.n; ++i) {
                if (a.type(i) == 1) std::printf("%s%g", i ? ", " : "", a.get_float(i));
                else if (a.type(i) == 2) std::printf("%s\"%s\"", i ? ", " : "", a.get_string(i).c_str());
                else if (a.type(i) == 3) std::printf("%s&", i ? ", " : "");
                else std::printf("%s%d", i ? ", " : "", a.get_int(i));
            }
            std::printf(")\n");
            (void)id;
        };
        if (std::string(argv[3]) == "all") {   // every function of the file, quietly
            rt.trace = nullptr;
            for (size_t k = 0; k < prog->function_count(); ++k) {
                const int id = prog->function_at(k).first;
                rt.talk_npc_info = std::getenv("DC_TALK") ? std::atoi(std::getenv("DC_TALK")) : 0;
                const bool started = rt.start(id);
                int f = 0;
                bool alive = started;
                for (; alive && f < frames; ++f) {
                    rt.pad_down = (f % 20 == 5) ? pad : 0;
                    rt.pad_on = rt.pad_down;
                    alive = rt.step();
                }
                std::printf("%s fn %d: started %d, %s after %d frames, result %d\n", map.c_str(), id, started ? 1 : 0, alive ? "RUNNING" : "ended", f, rt.result_code());
            }
            std::printf("unimplemented:");
            for (int id : rt.unknown_ext_ids) std::printf(" %d(%s)", id, ev_ext_name(id));
            std::printf("\n");
            return 0;
        }
        rt.talk_npc_info = std::getenv("DC_TALK") ? std::atoi(std::getenv("DC_TALK")) : 0;
        std::printf("start %d: %d\n", fid, rt.start(fid) ? 1 : 0);
        std::string last_text;
        int f = 0;
        for (; f < frames; ++f) {
            rt.pad_down = (f % 20 == 5) ? pad : 0;   // press now and then
            rt.pad_on = rt.pad_down;
            const bool alive = rt.step();
            for (size_t w = 0; w < rt.windows.size(); ++w) {
                const std::string t = rt.windows[w].visible_text();
                if (!t.empty() && rt.windows[w].state() >= MesWindow::Complete && t != last_text) {
                    std::printf("[win %zu] %s\n", w, t.c_str());
                    last_text = t;
                }
            }
            if (!alive) break;
        }
        std::printf("end after %d frames, result %d, nav %d, next event %d\n", f, rt.result_code(), int(rt.nav().kind), rt.next_event());
        std::printf("hero at (%g %g %g) yaw %g; talk list:", rt.actor(-1).pos.x, rt.actor(-1).pos.y, rt.actor(-1).pos.z, rt.actor(-1).rot.y);
        for (int m : rt.talk_list) std::printf(" %d", m);
        std::printf("\n");
        if (!rt.talk_list.empty() && rt.has_talk_bank) std::printf("talk: %s\n", rt.talk_bank.get(rt.talk_list[0], rt.hero_names).c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
