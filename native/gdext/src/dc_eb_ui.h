#pragma once
#include <godot_cpp/variant/dictionary.hpp>
#include <dccore/eb_duel.h>

namespace godot {

// What the EB duel's overlay (godot/eb_overlay.gd) draws; empty when no duel (or its intro) is running.
inline Dictionary eb_ui_dictionary(const dccore::EbDuel *eb) {
    Dictionary d;
    if (!eb || (!eb->active() && !eb->intro_active())) return d;
    d["active"] = eb->active();
    d["intro"] = eb->intro_active() ? eb->intro_count() : -1;
    d["count"] = eb->count();
    d["speed"] = eb->speed();
    d["key_count"] = eb->key_count();
    d["popup_timer"] = eb->popup_timer();
    d["popup_good"] = eb->popup_good();
    d["popup_key"] = eb->popup_key();
    d["result"] = eb->result();
    d["finish"] = eb->finish_count();
    Array keys;
    for (const dccore::EbKey &k : eb->keys()) {
        Dictionary kd;
        kd["tick"] = k.tick;
        kd["mask"] = int(k.mask);
        kd["done"] = k.done;
        kd["hint"] = k.hint;
        keys.push_back(kd);
    }
    d["keys"] = keys;
    return d;
}

}  // namespace godot
