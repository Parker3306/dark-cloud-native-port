#pragma once
#include <map>
#include <set>
#include <godot_cpp/variant/transform3d.hpp>
#include <memory>
#include <vector>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <dccore/dungeon.h>
#include <dccore/floor.h>

namespace godot {

// One generated dungeon floor: every cell's pieces placed exactly as the game's CDungeonParts does it (cell = 160 units, quarter turns about +Y).
// Layout comes from dccore (verified cell for cell against the game); this node only turns it into meshes.
class DcDungeon : public Node3D {
    GDCLASS(DcDungeon, Node3D)

    std::shared_ptr<dccore::Dungeon> dungeon_;
    dccore::FloorLayout layout_;
    Dictionary info_;
    std::set<std::string> dyn_names_;   // the objects the dungeon's scripts address by name (_GET_OBJHDL): they get a node of their own
    struct DynObject { Node3D *node; Transform3D cell; Vector3 pivot; bool top; };   // top: hangs under the floor node (placed with the cell's transform); else under another dynamic object
    std::map<std::string, std::vector<DynObject>> objects_;
    std::map<int, std::vector<std::pair<int, MeshInstance3D *>>> door_nodes_;   // per cell: the sliding doors (door kind, node), hidden when the door opens

protected:
    static void _bind_methods();

public:
    void setup(std::shared_ptr<dccore::Dungeon> dungeon, const dccore::FloorLayout &layout);
    Dictionary get_info() const { return info_; }
    Vector3 get_start_position() const;   // world position of the start cell (flag 0x200)
    Vector3 get_center() const;           // centre of the used 16 x 16 area
    int get_room_count() const { return int(layout_.rooms.size()); }
    Dictionary get_environment() const;
    void set_dynamic_objects(const std::set<std::string> &names) { dyn_names_ = names; }   // before setup
    void set_object(const String &name, const Vector3 &offset, const Vector3 &rotation);   // a script moved / turned a frame (the offset and angles are in the part's own axes)
    void set_object_visible(const String &name, bool visible);
    void set_back(bool b) { back_ = b; }   // the back floor ("ura") is lit by the cfg's URA_* set
    bool back_ = false;
    void open_door(int x, int y, int kind);   // a door (kind 1 locked door, 2 exit gate) of that cell opened: its slabs disappear   // fog / ambient / lights / background from the cfg ("front" floor)
};

} // namespace godot
