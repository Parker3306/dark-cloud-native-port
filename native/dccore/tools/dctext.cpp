// dctext <iso> <bank path in the game> [id ...]: prints the messages of a bank (all of them when no id is given)
#include <cstdio>
#include <cstdlib>
#include "dccore/disc.h"
#include "dccore/text.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: dctext <iso> <bank> [id...]\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const MessageBank bank = MessageBank::parse(disc.read(argv[2]));
        const std::vector<std::string> names = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};
        if (argc > 3) {
            for (int i = 3; i < argc; ++i) std::printf("%s: %s\n", argv[i], bank.get(std::atoi(argv[i]), names).c_str());
        } else {
            for (int id : bank.ids()) std::printf("%d: %s\n", id, bank.get(id, names).c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
