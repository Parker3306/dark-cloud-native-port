#pragma once
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <vector>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <dccore/town.h>
#include <dccore/walk.h>
#include "dc_character.h"

namespace godot {

// A Georama town or story scene as meshes: static objects and placed parts, the sky. Everything comes from dccore::Town.
class DcTown : public Node3D {
    GDCLASS(DcTown, Node3D)

    std::shared_ptr<dccore::Town> town_;
    std::vector<Node3D *> instance_nodes_;                       // one root per instance (null when it has no model)
    struct FrameRef { Node3D *node = nullptr; Vector3 pivot; };
    std::vector<std::map<std::string, std::vector<FrameRef>>> inst_frames_;   // per instance: its separately movable frames by name
    struct Anim {                                                // OBJ_ANIME
        Node3D *node = nullptr;
        Vector3 pivot;
        int kind = 0, mode = 0, steps = 0;
        float init[3] = {}, delta[3] = {}, end[3] = {}, cur[3] = {};
        int dir = 1;
        bool started = false;
        int instance = -1;
    };
    std::vector<Anim> anims_;
    struct Timer { Node3D *node = nullptr; float t0 = 0, t1 = 24; int instance = -1; };   // a frame shown only between two clock hours
    std::vector<Timer> timers_;
    std::function<void(size_t)> build_instance_;
    std::vector<std::set<std::string>> inst_extra_;   // frames split out on request
    void ensure_frame(int instance, const std::string &key);
    double anim_accum_ = 0, clock_ = 0;
    uint32_t rng_state_ = 12345;
    float frand();
    void step_anims();
    Node3D *sky_root_ = nullptr;
    std::map<int, Node3D *> sky_layers_;
    std::vector<Ref<StandardMaterial3D>> water_mats_;
    std::vector<Ref<ArrayMesh>> water_meshes_, asset_meshes_;
    std::function<Ref<StandardMaterial3D>(const std::string &)> material_for_, water_material_;
    Ref<ArrayMesh> asset_mesh(int asset);
    Ref<ArrayMesh> asset_water_mesh(int asset);
    void add_water(int instance);
    // CWater: the animated water sheets (WATER_SURFACE): a grid of heights that follows the wave equation (Hamon), shaken at random cells
    struct Sheet {
        MeshInstance3D *node = nullptr;
        Ref<ArrayMesh> mesh;
        const dccore::WaterSurfaceDef *def = nullptr;
        std::vector<float> h[2];
        int cur = 0;
    };
    std::vector<Sheet> sheets_;
    void build_sheets();
    void step_sheet(Sheet &s);
    void update_sheet(Sheet &s, const Vector3 &camera_pos, const Vector3 &camera_dir);
    double water_time_ = 0;

protected:
    static void _bind_methods();

public:
    void setup(std::shared_ptr<dccore::Town> town);
    void _process(double delta) override;
    std::shared_ptr<dccore::Town> town() const { return town_; }
    Dictionary get_info() const;
    // The light of the map at game time t (0..12, see notes/georama.md section 6.4): ambient, directional lights, fog and sky colours of the two light sets
    // around t blended by the fractional part.
    Dictionary get_light(double t) const;
    // Shows the sky layer of the time of day and moves the sky around the camera.
    void update_sky(double t, const Vector3 &camera_pos);
    Array get_events() const;
    Array get_people() const;
    void refresh_instance(int index);                    // the part changed (a road got another shape): build its nodes again
    Node3D *make_ghost(int asset);                       // a preview copy of a part's model (not part of the scene, the caller owns it)
    int add_instance(int index = -1);                    // builds the nodes of an instance appended to the town's list (placing a part); -1: the last one
    void set_instance_offset(int index, float dy);       // lifts an instance (the build drop effect)                                  // builds the nodes of the instance just appended to the town's list (placing a part); returns its index
    void remove_instance(int index);                     // the part was taken away
    void set_clock(double game_time) { clock_ = game_time; }   // game time 0..12: drives the timer frames (window lights...)
    void set_frame_visible(int instance, const String &frame, bool visible);
    void set_frame_transform(int instance, const String &frame, const Vector3 &pos, const Vector3 &rot, bool set_pos = true, bool set_rot = true);
};

} // namespace godot
