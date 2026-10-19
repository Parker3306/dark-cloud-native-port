#pragma once
// The game's script virtual machine (CRunScript::exe): one bytecode used by dungeon events, town events and every monster's `.stb` file.
// Instructions are three 32-bit words {opcode, a, b}; the machine has an 8-byte-slot stack ({type, value}: 0 int, 1 float, 2 string, 3 pointer to a slot),
// local frames, calls, and "external functions" (numbered, implemented by the host game code) that are called with their arguments on the stack.
// Ported from the decompiled exe/run/resume/skip/call_func/ret_func; checked on the real scripts with tools/dcstb (disassembler) and in the game.
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

struct Slot {
    int32_t type = 0;   // 0 int, 1 float, 2 string (value = byte offset in the program), 3 pointer (value = stack slot index)
    int32_t value = 0;
    float as_float() const {
        float f;
        std::memcpy(&f, &value, 4);
        return f;
    }
    static Slot make_float(float f) {
        Slot s;
        s.type = 1;
        std::memcpy(&s.value, &f, 4);
        return s;
    }
};

// A loaded `.stb`: header {magic 'STB\0', default function record, code base, function table offset, function count, ...}; function table entries are
// {id, offset of the function record}; a record is {entry (relative to the code base), ?, number of slots, number of arguments}.
class ScriptProgram {
public:
    explicit ScriptProgram(Bytes data);
    const Bytes& data() const { return data_; }
    uint32_t code_base() const { return code_base_; }
    bool has_function(int32_t id) const;
    uint32_t function_record(int32_t id) const;   // offset (from the start of the file) of the record of function `id`, or 0
    uint32_t default_record() const { return rd32(4); }
    std::string string_at(uint32_t program_offset) const;   // NUL terminated string at an offset from the file start
    uint32_t rd32(size_t off) const;
    size_t function_count() const { return count_; }
    std::pair<int32_t, uint32_t> function_at(size_t i) const;

private:
    Bytes data_;
    uint32_t code_base_ = 0, table_ = 0, count_ = 0;
};

class ScriptVm;

// What an external function sees: `n` argument slots (after the function id) on the VM stack.
struct ScriptArgs {
    ScriptVm* vm;
    int base;     // stack index of the first argument
    int n;
    int32_t get_int(int i) const;      // GetStackInt: floats are truncated
    float get_float(int i) const;      // GetStackFloat
    std::string get_string(int i) const;
    int type(int i) const;
    void set_int(int i, int32_t v) const;     // SetStack: writes through a pointer argument (type 3), otherwise nothing
    void set_float(int i, float v) const;
};

class ScriptVm {
public:
    // Host callback for external function `id`.
    using External = std::function<bool(int32_t id, const ScriptArgs& args)>;

    ScriptVm() = default;
    void load(std::shared_ptr<ScriptProgram> program, size_t stack_slots = 128, size_t call_slots = 512);
    void set_external(External ext) { ext_ = std::move(ext); }

    // Starts function `id` (a negative id starts the default function) with optional argument values. Like the game: returns -1 if the function does not
    // exist, 0 if the script ran to its end, 1 if it is still running (it yielded; call resume() next frame).
    int run(int32_t id, const std::vector<Slot>& args = {});
    int resume();                   // continues after a yield; returns 0 when finished, 1 when it yielded again
    bool running() const { return pc_ != 0; }
    uint32_t pc() const { return pc_; }   // file offset of the next instruction (0 when not running)
    void abort() { pc_ = 0; done_ = true; calls_.clear(); }   // the engine drops the script (a request like _GO_DUNGEON ends the event whatever the VM is doing)
    void skip();                    // the game's skip(): runs until the end of the skippable section ignoring yields
    bool has_function(int32_t id) const { return prog_ && prog_->has_function(id); }
    int32_t return_value() const { return ret_; }
    const ScriptProgram& program() const { return *prog_; }

    std::vector<Slot>& stack() { return stack_; }
    uint64_t instructions_executed = 0;
    uint64_t instruction_limit = 0;   // when non-zero: the script is stopped (as finished) after that many instructions in total; for hosts that cannot answer its waits
    bool trace = false;
    std::function<void(const std::string&)> log;

private:
    friend struct ScriptArgs;
    struct Frame { uint32_t pc, base, record; };
    static constexpr uint32_t kPrefix = 16;   // slots below the stack base that hold the entry function's arguments
    std::shared_ptr<ScriptProgram> prog_;
    External ext_;
    std::vector<Slot> stack_;
    uint32_t sp_ = 0;              // next free slot
    uint32_t base_ = 0;            // start of the current frame's slots
    uint32_t record_ = 0;          // current function record (file offset)
    uint32_t pc_ = 0;              // file offset of the next instruction, 0 = not running
    std::vector<Frame> calls_;
    size_t call_limit_ = 512;
    bool skip_ = false;
    bool done_ = false;
    int32_t ret_ = 0;

    int exec();
    void push(Slot s);
    Slot pop();
};

// Mnemonic text of one instruction (for the disassembler tool and tracing).
std::string disassemble(const ScriptProgram& p, uint32_t pc);

}  // namespace dccore
