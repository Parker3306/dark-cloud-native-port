#include "dc_sound.h"

#include <cstring>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void DcSound::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ready"), &DcSound::is_ready);
    ClassDB::bind_method(D_METHOD("load_bgm", "n"), &DcSound::load_bgm);
    ClassDB::bind_method(D_METHOD("play_bgm", "index"), &DcSound::play_bgm, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("stop_bgm", "fade_frames"), &DcSound::stop_bgm, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("bgm_fade_in", "frames", "target"), &DcSound::bgm_fade_in, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("bgm_fade_out", "frames"), &DcSound::bgm_fade_out);
    ClassDB::bind_method(D_METHOD("bgm_playing"), &DcSound::bgm_playing);
    ClassDB::bind_method(D_METHOD("bgm_fading"), &DcSound::bgm_fading);
    ClassDB::bind_method(D_METHOD("bgm_number"), &DcSound::bgm_number);
    ClassDB::bind_method(D_METHOD("load_sound_set", "n"), &DcSound::load_sound_set);
    ClassDB::bind_method(D_METHOD("load_music_pack", "data", "stem"), &DcSound::load_music_pack);
    ClassDB::bind_method(D_METHOD("play_note", "port", "prog", "key"), &DcSound::play_note);
    ClassDB::bind_method(D_METHOD("stop_note", "prog", "key"), &DcSound::stop_note);
    ClassDB::bind_method(D_METHOD("play_ambient", "index"), &DcSound::play_ambient, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("stop_ambient"), &DcSound::stop_ambient);
    ClassDB::bind_method(D_METHOD("play_se", "id", "vol", "pan"), &DcSound::play_se, DEFVAL(-1.0), DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("play_se_at", "id", "pos"), &DcSound::play_se_at);
    ClassDB::bind_method(D_METHOD("stop_se", "id"), &DcSound::stop_se);
    ClassDB::bind_method(D_METHOD("stop_all_se"), &DcSound::stop_all_se);
    ClassDB::bind_method(D_METHOD("set_listener", "pos", "dir"), &DcSound::set_listener);
    ClassDB::bind_method(D_METHOD("load_voice", "character"), &DcSound::load_voice);
    ClassDB::bind_method(D_METHOD("load_special", "n"), &DcSound::load_special);
    ClassDB::bind_method(D_METHOD("play_special", "index"), &DcSound::play_special, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("stop_special"), &DcSound::stop_special);
    ClassDB::bind_method(D_METHOD("set_master_volume", "v"), &DcSound::set_master_volume);
}

void DcSound::setup(std::unique_ptr<dccore::snd::SoundSystem> sys) {
    sys_ = std::move(sys);
    player_ = memnew(AudioStreamPlayer);
    Ref<AudioStreamGenerator> gen;
    gen.instantiate();
    gen->set_mix_rate(48000.0f);
    gen->set_buffer_length(0.25f);
    player_->set_stream(gen);
    add_child(player_);
    set_process(true);
}

void DcSound::_ready() {
    if (!player_) return;
    player_->play();
    playback_ = player_->get_stream_playback();
}

void DcSound::_process(double) {
    if (!sys_ || playback_.is_null()) return;
    int frames = int(playback_->get_frames_available());
    if (frames <= 0) return;
    frames = std::min(frames, 8192);
    scratch_.resize(size_t(frames) * 2);
    sys_->render(scratch_.data(), frames);
    PackedVector2Array buf;
    buf.resize(frames);
    Vector2 *w = buf.ptrw();
    for (int i = 0; i < frames; ++i) w[i] = Vector2(scratch_[size_t(i) * 2], scratch_[size_t(i) * 2 + 1]);
    playback_->push_buffer(buf);
}

void godot::DcSound::load_music_pack(const PackedByteArray &data, const String &stem) {
    if (!sys_) return;
    try {
        dccore::Bytes b(size_t(data.size()));
        std::memcpy(b.data(), data.ptr(), size_t(data.size()));
        sys_->load_music_pack(std::move(b), stem.utf8().get_data());
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcSound.load_music_pack: ") + e.what());
    }
}
