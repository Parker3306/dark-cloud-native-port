#pragma once
#include <map>
#include <memory>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <dccore/motion.h>
#include <vector>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <dccore/character.h>

namespace godot {

// A playable character or monster: a Skeleton3D driven by the game's own motion data, a GPU-skinned body and rigid parts that follow their joints.
// All animation maths is in dccore (verified against the running game); this node only moves bones and meshes.
class DcCharacter : public Node3D {
    GDCLASS(DcCharacter, Node3D)

    std::shared_ptr<dccore::Character> ch_;
    Skeleton3D *skeleton_ = nullptr;
    struct Rigid { MeshInstance3D *node; int object; };
    std::vector<Rigid> rigid_;
    int motion_ = 0;
    double time_ = 0.0;
    double speed_scale_ = 1.0;
    bool playing_ = true;
    bool loop_ = true;

    std::map<std::string, Ref<StandardMaterial3D>> tex_materials_;   // by texture name (the co-op colour swaps their textures)
    int color_index_ = -1;
    int cur_set_ = -1;   // the extra motion file of the motion playing now (-1: the model's own)
    DcCharacter *held_ = nullptr;   // a weapon model that hangs on a joint of this one (co-op puppets)
    int held_joint_ = -1;
    dccore::Pose pose_;   // the pose last applied
    void apply_pose();

protected:
    static void _bind_methods();

public:
    void setup(std::shared_ptr<dccore::Character> ch, Dictionary &info);  // builds all child nodes
    void _process(double delta) override;

    std::shared_ptr<dccore::Character> get_character() const { return ch_; }
    // Where an attachment with the local matrix `local` (row-vector convention) on joint `joint` is, in this character's space, for the pose shown now.
    Transform3D attach_transform(int joint, const dccore::Mat4 &local) const;
    int get_motion_count() const;
    Dictionary get_motion_info(int index) const;
    void play(int motion, bool loop = true);
    void set_time(double frame);
    double get_time() const { return time_; }
    void set_playing(bool p) { playing_ = p; }
    bool is_playing() const { return playing_; }
    void set_speed_scale(double s) { speed_scale_ = s; }
    double get_speed_scale() const { return speed_scale_; }
    int get_motion() const { return motion_; }
    PackedStringArray get_joint_names() const;
    Dictionary sample_camera(double frame) const;   // scene motions: {pos, ref, fov} of the camera tracks at that frame (empty when there are none)
    int find_joint(const String &name) const;       // the object with that name (frame names compare up to the first "__"), -1 if none
    void set_effect_look(bool on);   // effect models: additive blending, unshaded, both sides, no depth writes
    // Co-op: the character wears player `index`'s colour (clothes recoloured, see dccore/recolor.h); -1 = the original colours
    void set_player_color(int index);
    void set_held_weapon(DcCharacter *weapon, int joint);
    void set_character(std::shared_ptr<dccore::Character> ch) { ch_ = std::move(ch); cur_set_ = -1; }   // the same nodes with a character that has more motions (an event added them)   // the weapon follows the joint (a puppet holds his sword)
    int get_player_color() const { return color_index_; }
};

} // namespace godot
