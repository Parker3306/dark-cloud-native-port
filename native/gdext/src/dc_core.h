#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {
// Thin Godot-facing wrapper around dccore. Everything with game meaning lives in dccore; this class only converts types.
class DcCore : public RefCounted {
    GDCLASS(DcCore, RefCounted)
protected:
    static void _bind_methods();
public:
    String get_version() const;
};
}
