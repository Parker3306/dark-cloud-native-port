// Disassembles a monster/event STB script: dcdisasm <iso> <path in DATA> <function id> [instructions]
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "dccore/disc.h"
#include "dccore/script.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: dcdisasm <iso> <path> <function id> [count]\n");
        return 2;
    }
    try {
        dccore::Disc disc{std::filesystem::path(argv[1])};
        dccore::ScriptProgram prog(disc.read(argv[2]));
        const int id = std::atoi(argv[3]);
        const int count = argc > 4 ? std::atoi(argv[4]) : 200;
        if (id == -1) {
            for (size_t i = 0; i < prog.function_count(); ++i) std::printf("function %d at 0x%x\n", prog.function_at(i).first, prog.function_at(i).second);
            return 0;
        }
        if (id == -3) {   // from a file offset: dcdisasm iso path -3 <count> <start>
            uint32_t pc = uint32_t(std::strtoul(argv[5], nullptr, 0));
            for (int i = 0; i < count && pc + 12 <= prog.data().size(); ++i, pc += 12) std::printf("%04x: %s\n", pc, dccore::disassemble(prog, pc).c_str());
            return 0;
        }
        if (id == -2) {   // the whole code section
            for (uint32_t pc = prog.code_base(); pc + 12 <= prog.data().size(); pc += 12) std::printf("%04x: %s\n", pc, dccore::disassemble(prog, pc).c_str());
            return 0;
        }
        const uint32_t rec = prog.function_record(id);
        if (!rec) { std::puts("no such function"); return 1; }
        std::printf("record 0x%x slots %u args %u\n", rec, prog.rd32(rec + 8), prog.rd32(rec + 0xc));
        uint32_t pc = prog.code_base() + prog.rd32(rec);
        for (int i = 0; i < count && pc + 12 <= prog.data().size(); ++i, pc += 12) std::printf("%04x: %s\n", pc, dccore::disassemble(prog, pc).c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
