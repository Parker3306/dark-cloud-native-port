#include "dccore/script.h"
#include "dccore/gamemath.h"
#include "dccore/libm_ps2.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dccore {

ScriptProgram::ScriptProgram(Bytes data) : data_(std::move(data)) {
    if (data_.size() < 0x20 || std::memcmp(data_.data(), "STB", 4) != 0) throw Error("not an STB script");
    code_base_ = rd32(8);
    table_ = rd32(0xc);
    count_ = rd32(0x10);
    if (size_t(table_) + size_t(count_) * 8 > data_.size() || code_base_ > data_.size()) throw Error("bad STB header");
}

uint32_t ScriptProgram::rd32(size_t off) const {
    if (off + 4 > data_.size()) throw Error("script read out of range");
    uint32_t v;
    std::memcpy(&v, data_.data() + off, 4);
    return v;
}

std::pair<int32_t, uint32_t> ScriptProgram::function_at(size_t i) const {
    return {int32_t(rd32(table_ + i * 8)), rd32(table_ + i * 8 + 4)};
}

bool ScriptProgram::has_function(int32_t id) const { return function_record(id) != 0; }

uint32_t ScriptProgram::function_record(int32_t id) const {
    for (size_t i = 0; i < count_; ++i) {
        auto f = function_at(i);
        if (f.first == id) return f.second;
    }
    return 0;
}

std::string ScriptProgram::string_at(uint32_t off) const {
    std::string s;
    for (size_t i = off; i < data_.size() && data_[i]; ++i) s += char(data_[i]);
    return s;
}

int32_t ScriptArgs::get_int(int i) const {
    const Slot& s = vm->stack_[size_t(base + i)];
    if (s.type == 1) return int32_t(s.as_float());
    return s.value;
}
float ScriptArgs::get_float(int i) const {
    const Slot& s = vm->stack_[size_t(base + i)];
    return s.type == 0 ? float(s.value) : s.as_float();
}
std::string ScriptArgs::get_string(int i) const { return vm->prog_->string_at(uint32_t(vm->stack_[size_t(base + i)].value)); }
int ScriptArgs::type(int i) const { return vm->stack_[size_t(base + i)].type; }
void ScriptArgs::set_int(int i, int32_t v) const {
    const Slot& s = vm->stack_[size_t(base + i)];
    if (s.type == 3) vm->stack_[size_t(s.value)].value = v;
}
void ScriptArgs::set_float(int i, float v) const {
    const Slot& s = vm->stack_[size_t(base + i)];
    if (s.type == 3) std::memcpy(&vm->stack_[size_t(s.value)].value, &v, 4);
}

void ScriptVm::load(std::shared_ptr<ScriptProgram> program, size_t stack_slots, size_t call_slots) {
    prog_ = std::move(program);
    stack_.assign(stack_slots + kPrefix, Slot{});
    call_limit_ = call_slots;
    calls_.clear();
    sp_ = kPrefix;
    base_ = kPrefix;
    pc_ = 0;
    skip_ = false;
    done_ = true;
}

void ScriptVm::push(Slot s) {
    if (sp_ >= stack_.size()) throw Error("script stack overflow");
    stack_[sp_++] = s;
}

Slot ScriptVm::pop() {
    if (sp_ == 0) throw Error("script stack underflow");
    return stack_[--sp_];
}

int ScriptVm::run(int32_t id, const std::vector<Slot>& args) {
    if (!prog_) return -1;
    const uint32_t rec = id < 0 ? prog_->default_record() : prog_->function_record(id);
    if (rec == 0) return -1;
    calls_.clear();
    skip_ = false;
    done_ = false;
    record_ = rec;
    const uint32_t nargs = prog_->rd32(rec + 0xc), nslots = prog_->rd32(rec + 8);
    base_ = kPrefix - nargs;
    for (size_t i = 0; i < args.size() && i < nargs; ++i) stack_[base_ + i] = args[i];
    sp_ = base_ + nslots;
    for (uint32_t i = nargs; i < nslots; ++i) stack_[base_ + i] = Slot{};
    pc_ = prog_->code_base() + prog_->rd32(rec);
    exec();
    return done_ ? 0 : 1;
}

int ScriptVm::resume() {
    if (pc_ != 0) exec();
    return done_ ? 0 : 1;
}

void ScriptVm::skip() {
    skip_ = true;
    resume();
}

int ScriptVm::exec() {
    const ScriptProgram& P = *prog_;
    // Addressing of load/address instructions (cases 1 and 2 of the decompiled exe): modes 4/0x20 index through the pointer held in the slot,
    // modes 2/0x10 index the frame itself, 1/8 are plain variables; 8, 0x10 and 0x20 also mark the slot as a float.
    auto slot_index = [&](uint32_t w1, uint32_t mode, bool& force_float) -> uint32_t {
        force_float = (mode == 0x20 || mode == 0x10 || mode == 8);
        switch (mode) {
            case 0x20:
            case 4: {
                const int32_t idx = pop().value;
                return uint32_t(stack_[base_ + w1].value + idx);
            }
            case 0x10:
            case 2: {
                const int32_t idx = pop().value;
                return base_ + w1 + uint32_t(idx);
            }
            default: return base_ + w1;
        }
    };
    for (;;) {
        const uint32_t op = P.rd32(pc_), a = P.rd32(pc_ + 4), b = P.rd32(pc_ + 8);
        ++instructions_executed;
        if (instruction_limit && instructions_executed > instruction_limit) {
            done_ = true;
            pc_ = 0;
            return 0;
        }
        if (trace && log) log(disassemble(P, pc_));
        switch (op) {
            case 1: {
                bool ff;
                const uint32_t i = slot_index(a, b, ff);
                if (i >= stack_.size()) throw Error("script variable out of range");
                if (ff) stack_[i].type = 1;
                push(stack_[i]);
                break;
            }
            case 2: {
                bool ff;
                const uint32_t i = slot_index(a, b, ff);
                push(Slot{3, int32_t(i)});
                break;
            }
            case 3:
                if (a == 1) push(Slot{0, int32_t(b)});
                else if (a == 3) push(Slot{2, int32_t(P.code_base() + b)});
                else if (a == 2) push(Slot{1, int32_t(b)});
                break;
            case 4: --sp_; break;
            case 5: {
                Slot v = pop();
                Slot dst = pop();
                if (dst.type == 3) stack_[size_t(dst.value)] = v;
                push(v);
                break;
            }
            case 6: case 7: case 8: case 9: {
                Slot r = pop(), l = pop();
                if (op == 9 && r.value == 0 && r.type == 0) throw Error("script division by zero");
                auto F = [](const Slot& s) { return s.type == 0 ? float(s.value) : s.as_float(); };
                if (l.type == 0 && r.type == 0) {
                    const int32_t x = l.value, y = r.value;
                    int32_t z = 0;
                    if (op == 6) z = int32_t(uint32_t(x) + uint32_t(y));
                    else if (op == 7) z = int32_t(uint32_t(x) - uint32_t(y));
                    else if (op == 8) z = int32_t(uint32_t(x) * uint32_t(y));
                    else z = y == 0 ? 0 : x / y;
                    push(Slot{0, z});
                } else {
                    const float x = F(l), y = F(r);
                    float z = 0;
                    if (op == 6) z = gm::fadd(x, y);
                    else if (op == 7) z = gm::fsub(x, y);
                    else if (op == 8) z = gm::fmul(x, y);
                    else z = gm::fdiv(x, y);
                    push(Slot::make_float(z));
                }
                break;
            }
            case 10: {
                const int32_t r = pop().value, l = pop().value;
                if (r == 0) throw Error("script modulo by zero");
                push(Slot{0, l % r});
                break;
            }
            case 0xb: {
                Slot v = pop();
                if (v.type == 0) push(Slot{0, -v.value});
                else push(Slot::make_float(-v.as_float()));
                break;
            }
            case 0xc: {
                Slot v = pop();
                push(Slot::make_float(v.type == 0 ? float(v.value) : v.as_float()));
                break;
            }
            case 0xd: {
                Slot v = pop();
                push(Slot{0, v.type == 0 ? v.value : int32_t(v.as_float())});
                break;
            }
            case 0xe: {
                Slot r = pop(), l = pop();
                bool res = false;
                if (l.type == 0 && r.type == 0) {
                    const int32_t x = l.value, y = r.value;
                    switch (a) {
                        case 0x28: res = y == x; break;
                        case 0x29: res = y != x; break;
                        case 0x2a: res = x < y; break;
                        case 0x2b: res = x <= y; break;
                        case 0x2c: res = y < x; break;
                        case 0x2d: res = y <= x; break;
                    }
                } else {
                    const float x = l.type == 0 ? float(l.value) : l.as_float(), y = r.type == 0 ? float(r.value) : r.as_float();
                    switch (a) {
                        case 0x28: res = y == x; break;
                        case 0x29: res = y != x; break;
                        case 0x2a: res = x < y; break;
                        case 0x2b: res = x <= y; break;
                        case 0x2c: res = y < x; break;
                        case 0x2d: res = y <= x; break;
                    }
                }
                push(Slot{0, res ? 1 : 0});
                break;
            }
            case 0xf: {
                Slot v = pop();
                if (calls_.empty()) {
                    ret_ = v.value;
                    push(v);
                    pc_ = 0;
                    done_ = true;
                    return 0;
                }
                sp_ = base_;
                Frame f = calls_.back();
                calls_.pop_back();
                base_ = f.base;
                record_ = f.record;
                pc_ = f.pc;
                push(v);
                break;
            }
            case 0x10:
                if (!skip_) {
                    pc_ = P.code_base() + a;
                    continue;
                }
                break;
            case 0x11:
                if (!skip_) {
                    Slot v = pop();
                    if (!(v.type != 0 || v.value != 0)) {
                        if (b != 0) push(Slot{0, 0});
                        pc_ = P.code_base() + a;
                        continue;
                    }
                }
                break;
            case 0x12:
                if (!skip_) {
                    Slot v = pop();
                    if (v.type != 0 || v.value != 0) {
                        if (b != 0) push(Slot{0, 1});
                        pc_ = P.code_base() + a;
                        continue;
                    }
                }
                break;
            case 0x13: {
                if (calls_.size() >= call_limit_) throw Error("script call stack overflow");
                const uint32_t rec = P.code_base() + b;
                calls_.push_back({pc_, base_, record_});
                const uint32_t nargs = P.rd32(rec + 0xc), nslots = P.rd32(rec + 8);
                base_ = sp_ - nargs;
                sp_ = base_ + nslots;
                record_ = rec;
                for (uint32_t i = nargs; i < nslots; ++i) stack_[base_ + i] = Slot{};
                pc_ = P.code_base() + P.rd32(rec) - 12;   // the common "pc += 12" below lands on the entry
                break;
            }
            case 0x14: sp_ -= a; break;   // debug print
            case 0x15: {
                sp_ -= a;
                if (!skip_ && ext_) {
                    const int32_t id = stack_[sp_].value;
                    ScriptArgs args{this, int(sp_) + 1, int(a) - 1};
                    ext_(id, args);
                }
                break;
            }
            case 0x17:
                if (!skip_) {
                    pc_ += 12;
                    return 1;
                }
                break;
            case 0x18: { const int32_t r = pop().value, l = pop().value; push(Slot{0, l & r}); break; }
            case 0x19: { const int32_t r = pop().value, l = pop().value; push(Slot{0, l | r}); break; }
            case 0x1a: { Slot v = pop(); push(Slot{0, v.value == 0 ? 1 : 0}); break; }
            case 0x1b:
                done_ = true;
                pc_ = 0;
                return 0;
            case 0x1c:
                if (skip_) {
                    skip_ = false;
                    pc_ += 12;
                    return 1;
                }
                break;
            case 0x1d: { Slot v = pop(); push(Slot::make_float(ps2_sinf(v.type == 0 ? float(v.value) : v.as_float()))); break; }
            case 0x1e: { Slot v = pop(); push(Slot::make_float(ps2_cosf(v.type == 0 ? float(v.value) : v.as_float()))); break; }
            default: break;   // 0x16 and unknown opcodes do nothing
        }
        pc_ += 12;
    }
}

std::string disassemble(const ScriptProgram& p, uint32_t pc) {
    const uint32_t op = p.rd32(pc), a = p.rd32(pc + 4), b = p.rd32(pc + 8);
    char buf[160];
    static const char* names[] = {"?0", "load", "addr", "push", "drop", "assign", "add", "sub", "mul", "div", "mod", "neg", "tofloat", "toint", "cmp", "return",
                                  "jmp", "jf", "jt", "call", "print", "ext", "nop", "yield", "and", "or", "not", "end", "endskip", "sin", "cos"};
    const char* n = op < 31 ? names[op] : "?";
    std::string extra;
    if (op == 3 && a == 2) {
        float f;
        std::memcpy(&f, &b, 4);
        std::snprintf(buf, sizeof buf, " %g", f);
        extra = buf;
    } else if (op == 3 && a == 3) {
        extra = " \"" + p.string_at(p.code_base() + b) + "\"";
    }
    std::snprintf(buf, sizeof buf, "%06x  %-8s %u %u (0x%x)", pc, n, a, b, b);
    return std::string(buf) + extra;
}

}  // namespace dccore
