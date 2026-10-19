#pragma once
#include <memory>
#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/classes/audio_stream_generator_playback.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <dccore/sound.h>

namespace godot {

// The game's music and effects: dccore's SoundSystem rendered into an AudioStreamGenerator (48 kHz stereo). Add it to the tree; it fills the audio buffer every frame.
class DcSound : public Node {
    GDCLASS(DcSound, Node)

    std::unique_ptr<dccore::snd::SoundSystem> sys_;
    AudioStreamPlayer *player_ = nullptr;
    Ref<AudioStreamGeneratorPlayback> playback_;
    std::vector<float> scratch_;

protected:
    static void _bind_methods();

public:
    void setup(std::unique_ptr<dccore::snd::SoundSystem> sys);
    void _process(double delta) override;
    void _ready() override;
    bool is_ready() const { return sys_ != nullptr; }

    void load_bgm(int n) { if (sys_) sys_->load_bgm(n); }
    void play_bgm(int index = 0) { if (sys_) sys_->play_bgm(index); }
    void stop_bgm(int fade_frames = 0) { if (sys_) sys_->stop_bgm(fade_frames); }
    void bgm_fade_in(int frames, int target = -1) { if (sys_) sys_->bgm_fade_in(frames, target); }
    void bgm_fade_out(int frames) { if (sys_) sys_->bgm_fade_out(frames); }
    bool bgm_playing() const { return sys_ && sys_->bgm_playing(); }
    bool bgm_fading() const { return sys_ && sys_->bgm_fading(); }
    int bgm_number() const { return sys_ ? sys_->bgm_number() : -1; }
    void load_sound_set(int n) { if (sys_) sys_->load_sound_set(n); }
    void load_music_pack(const PackedByteArray &data, const String &stem);   // a pack that holds banks and music (the opening's scenes)
    void play_note(int port, int prog, int key) { if (sys_) sys_->play_pk(port, prog, key); }
    void stop_note(int prog, int key) { if (sys_) sys_->stop_pk(prog, key); }
    void play_ambient(int index = 0) { if (sys_) sys_->play_ambient(index); }
    void stop_ambient() { if (sys_) sys_->stop_ambient(); }
    void play_se(int id, double vol = -1.0, double pan = 0.0) { if (sys_) sys_->play_se(id, float(vol), float(pan)); }
    void play_se_at(int id, const Vector3 &pos) { if (sys_) sys_->play_se_at(id, {pos.x, pos.y, pos.z}); }
    void stop_se(int id) { if (sys_) sys_->stop_se(id); }
    void stop_all_se() { if (sys_) sys_->stop_all_se(); }
    void set_listener(const Vector3 &pos, const Vector3 &dir) { if (sys_) sys_->set_listener({pos.x, pos.y, pos.z}, {dir.x, dir.y, dir.z}); }
    void load_voice(int character) { if (sys_) sys_->load_voice(character); }
    void load_special(int n) { if (sys_) sys_->load_special(n); }
    void play_special(int index = 0) { if (sys_) sys_->play_special(index); }
    void stop_special() { if (sys_) sys_->stop_special(); }
    void set_master_volume(double v) { if (sys_) sys_->set_master_volume(float(v)); }
};

} // namespace godot
