#include <cstdio>
#include <cstring>
#include <span>
#include "dccore/version.h"
int main() {
    // C++20 check (std::span) plus a link check against the library.
    int a[3] = {1, 2, 3};
    std::span<int> s(a);
    if (s.size() != 3 || std::strcmp(dccore::version(), "0.0.1") != 0) return 1;
    std::printf("dccore %s ok\n", dccore::version());
    return 0;
}
