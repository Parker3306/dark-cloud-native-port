#pragma once
// Message banks: the game's text files (meswin/_systeme.bin for the system text, gedit/*/*.mes for the town, ...) are a table of {id, offset} followed by
// 16-bit codes: 0xFD21.. letters of the font, 0xFF00 line break, 0xFF01 end, 0xFF02 space, 0xFF03 "wait for a key, next page", 0xFAFA-0xFAFF the six
// character names. Text of the real thing is decoded here to UTF-8 (letters, digits and the common punctuation; other glyphs come out as '?').
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

class MessageBank {
public:
    static MessageBank parse(View data);
    bool has(int id) const { return index_.count(id) != 0; }
    // Decoded text of a message ("" when missing). `names`: replacement text for the character name codes 0xFAFA..0xFAFF (Toan, Xiao, Goro, Ruby, Ungaga, Osmond).
    std::string get(int id, const std::vector<std::string>& names = {}) const;
    std::vector<int> ids() const;
    size_t size() const { return index_.size(); }
    // The raw 16-bit codes of message `id` starting at its first code (up to and including the end code 0xFF01), empty when missing.
    std::vector<uint16_t> raw(int id) const;

private:
    std::vector<int16_t> codes_;
    std::map<int, size_t> index_;   // id -> index of the first code
};

// One element of a message with its control codes kept apart (the message window needs them).
struct MesToken {
    enum Kind { Glyph, LineFeed, Space, PageBreak, Colour, HeroName, SysSlot, Number, ValueSlot, Skip, Pause, Shake, Other } kind = Other;
    std::string text;   // Glyph: the decoded character(s)
    int arg = 0;        // HeroName: 0..5; SysSlot: 0.. (FBFE = 0, FBFD = 1, FBFC = 2, FBFB = 3, FBF2.. = 4..); ValueSlot: 0..7; Colour/Skip/Pause/Shake: the xx byte
};

// Tokens of message `id` up to its end code (empty when the id is missing).
std::vector<MesToken> mes_tokens(const MessageBank& bank, int id);

// Decodes one code (returns false for control codes); exposed for tools.
bool decode_glyph(uint16_t code, std::string& out);

// Name texts of the system bank: weapons are messages 101.. (weapon id - 156), every other item is its id + 100.
std::string item_text_id_name(const MessageBank& system, int item_id);

}  // namespace dccore
