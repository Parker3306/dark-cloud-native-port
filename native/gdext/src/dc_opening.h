#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <dccore/disc.h>
#include <dccore/floor.h>
#include <dccore/opening.h>
#include <dccore/pack.h>
#include <dccore/text.h>
#include "dc_character.h"

namespace godot {

// The opening cutscene (notes/opening.md): runs dccore::OpeningSim and shows its stage objects, camera and effects.
class DcOpening : public Node3D {
    GDCLASS(DcOpening, Node3D)

    dccore::Disc *disc_ = nullptr;
    std::unique_ptr<dccore::OpeningSim> sim_;
    dccore::Bytes title_;
    std::map<std::string, std::shared_ptr<dccore::SneClip>> clips_;
    std::shared_ptr<dccore::SneClip> clip_;
    dccore::MessageBank text_;
    Camera3D *camera_ = nullptr;
    Node3D *stage_ = nullptr;
    std::array<DcCharacter *, 23> chars_{};
    std::vector<DcCharacter *> dancers_;
    std::map<std::string, dccore::Image> textures_;                   // the scene's pim / pak images by texture name
    std::map<std::string, Ref<StandardMaterial3D>> materials_;
    struct Spin { Node3D *node; Vector3 axis_deg_per_step; };
    std::vector<Spin> spins_;
    std::vector<Node3D *> torches_;
    Dictionary env_;
    float projection_ = 800.0f;
    int loaded_scene_ = -1;
    double accumulator_ = 0.0;
    bool auto_run_ = true;
    int dance_phase_[2] = {0, 0};                                     // the leader's and the dancers' phase of the dance
    double dance_time_[2] = {0.0, 0.0};
    std::vector<std::array<int, 2>> dance_range_[2];                    // per phase: first and last frame (TITLE.BIN tables noroi / dancer)
    void load_dance_phase(int group, int phase);
    void step_dance();
    Array events_;
    float obj_time_[23] = {};
    int obj_motion_shown_[23];

    Ref<StandardMaterial3D> material_for(const std::string &tex);
    void add_images_from(const std::string &path);
    std::vector<std::shared_ptr<dccore::Pack>> model_packs_;           // packs that hold the scene's models by bare name
    std::vector<uint8_t> read_model(const std::string &path);
    void clear_stage();
    void load_scene(int scene);
    void load_cfg_scene(const std::string &cfg_path);
    void add_model(const std::string &path, const Vector3 &pos, const Vector3 &rot_deg, bool spin_frames, const std::map<std::string, Vector3> &spins);
    DcCharacter *make_character(const std::string &pack_path, const std::string &cfg_name, const std::string &mds_override = "");
    void apply_state();
    struct Effect { Node3D *node; int age; int life; float size; bool beam; };
    std::vector<Effect> effects_;
    void spawn_bomb(const Vector3 &pos, float size);
    void spawn_beam(const Vector3 &from, const Vector3 &to, bool flash_only);
    void step_effects();
    void step_objects();
    void apply_objects();

protected:
    static void _bind_methods();

public:
    void setup(dccore::Disc *disc);
    void _physics_process(double delta) override;
    void step_ticks(int n);
    void set_auto_run(bool on) { auto_run_ = on; }
    void set_auto_key(bool on) { if (sim_) sim_->auto_key = on; }
    void press_ok() { if (sim_) sim_->press_ok(); }
    void skip() { if (sim_) sim_->skip(); }
    Dictionary get_ui() const;
    Dictionary get_environment() const { return env_; }
    Array take_events() { Array a = events_; events_ = Array(); return a; }
    String get_message(int id) const;
    int get_steps() const { return sim_ ? int(sim_->steps) : 0; }
    bool is_finished() const { return sim_ && sim_->finished(); }
    int get_scene() const { return sim_ ? sim_->scene : 0; }
    Camera3D *get_camera() const { return camera_; }
    Dictionary get_debug() const;
    Dictionary get_music_pack(int scene) const;   // {data, stem}: the pack with the banks and the music of a scene (empty when it has none)
    void fast_forward(int steps);       // runs the script without drawing (for tools)
};

} // namespace godot
