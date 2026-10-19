// The EB duel's judging rules (notes/eb_duel.md section 3): the windows, the inner window, errors, the 80 / 160 tick results.
#include <cstdio>
#include "dccore/eb_duel.h"

using namespace dccore;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// One motion of 400 ticks, one key (mask 0x20) at motion time 20 (tick 40 at speed 0.5), arg 0.7 (S = 2.8): returns the result of a duel where the key is pressed at tick `at`.
static int play(int key_tick_offset, uint32_t extra_mask_at_press, int press_count) {
    EbDuel d;
    d.init(0.7f);
    d.set_motions({{250, 0, 200, 0.5f, 0}});   // 400 ticks
    d.set_key(20.0f, 0x20, 0);                 // tick 40
    int result = 0;
    for (int i = 0; i < 1000 && result == 0; ++i) {
        uint32_t down = 0;
        if (press_count > 0 && d.count() == 40 + key_tick_offset) { down = 0x20 | extra_mask_at_press; --press_count; }
        result = d.step(down);
    }
    return result;
}

int main() {
    CHECK(play(3, 0, 1) == 2);      // inside the inner window (+1..+8): EXCELLENT
    CHECK(play(0, 0, 1) == 1);      // on the line (0): accepted, but not inner: CLEAR
    CHECK(play(-4, 0, 1) == 1);     // early but accepted (-5..): CLEAR
    CHECK(play(-6, 0, 1) == -1);    // too early: MISS
    CHECK(play(17, 0, 1) == 1);     // late but accepted (..+17)
    CHECK(play(18, 0, 1) == -1);    // too late: the window ended unfinished
    CHECK(play(3, 0x10, 1) == -1);  // an extra button: MISS
    CHECK(play(3, 0, 0) == -1);     // never pressed: MISS
    // the timing of the result: success 160 ticks after the end of the motion list, a miss 80 ticks after the failing call
    {
        EbDuel d;
        d.init(0.7f);
        d.set_motions({{250, 0, 200, 0.5f, 0}});
        d.set_key(20.0f, 0x20, 0);
        int calls = 0, result = 0;
        while (result == 0 && calls < 2000) { result = d.step(d.count() == 43 ? 0x20 : 0); ++calls; }
        CHECK(result == 2);
        CHECK(calls == 400 + 160 + 1);   // the call that sees the end sets the counter, the 160th after it returns the result
    }
    {
        EbDuel d;
        d.init(0.7f);
        d.set_motions({{250, 0, 200, 0.5f, 0}});
        d.set_key(20.0f, 0x20, 0);
        int calls = 0, result = 0;
        while (result == 0 && calls < 2000) { result = d.step(calls == 5 ? 0x40 : 0); ++calls; }   // a wrong press with no window open
        CHECK(result == -1);
        CHECK(calls == 6 + 80);   // the failing call sets the counter, the 80th call after it returns the result
    }
    // two-bit mask: both bits inside the window (on different ticks) count
    {
        EbDuel d;
        d.init(0.7f);
        d.set_motions({{250, 0, 200, 0.5f, 0}});
        d.set_key(20.0f, 0x9000, 0);
        int result = 0;
        for (int i = 0; i < 1000 && result == 0; ++i) result = d.step(d.count() == 43 ? 0x1000 : (d.count() == 45 ? 0x8000 : 0));
        CHECK(result == 2);
    }
    // the key's tick: the motion list is walked in order
    {
        EbDuel d;
        d.init(1.0f);
        d.set_motions({{1, 10, 30, 1.0f, 0}, {2, 100, 140, 0.5f, 0}});   // 20 ticks, then 80 ticks
        d.set_key(120.0f, 0x40, 0);
        CHECK(d.keys()[0].tick == 20 + 40);
        d.set_key(500.0f, 0x40, 0);   // in no motion: the total
        CHECK(d.keys()[1].tick == 100);
    }
    if (failures == 0) std::printf("eb duel ok\n");
    return failures ? 1 : 0;
}
