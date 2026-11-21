#include "audio.h"
#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb_vorbis.c"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace xa {

static std::string upper(std::string s) { for (auto& c : s) c = char(std::toupper((unsigned char)c)); return s; }

bool Audio::init(const GameFS& fs) {
    fs_ = &fs;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    want.callback = &Audio::callback; want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev_) return false;
    rate_ = have.freq;
    voices_.resize(48);
    std::vector<uint8_t> raw;
    if (fs.read("DATA.SFX\\SOUND\\SAMPLES.SET", raw)) {
        std::istringstream in(std::string(raw.begin(), raw.end()));
        std::string n; int v;
        while (in >> n) { if (in >> v) volumes_[upper(n)] = v; else { in.clear(); } }
    }
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

void Audio::shutdown() { if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; } if (music_) { stb_vorbis_close(music_); music_ = nullptr; } }

bool Audio::playMusic(const std::string& path, bool loop) {
    if (!dev_ || !fs_) return false;
    std::vector<uint8_t> data;
    if (!fs_->read(path, data)) return false;
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(data.data(), int(data.size()), &err, nullptr);
    if (!v) return false;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    SDL_LockAudioDevice(dev_);
    if (music_) stb_vorbis_close(music_);
    musicData_.swap(data);          // the decoder keeps pointing into the moved buffer (same storage)
    music_ = v; musicChannels_ = info.channels; musicRate_ = int(info.sample_rate); musicLoop_ = loop;
    musicFrames_ = 0; musicPos_ = 0;
    SDL_UnlockAudioDevice(dev_);
    return true;
}

void Audio::stopMusic() {
    if (!dev_) return;
    SDL_LockAudioDevice(dev_);
    if (music_) { stb_vorbis_close(music_); music_ = nullptr; }
    SDL_UnlockAudioDevice(dev_);
}

int Audio::sound(const std::string& name) {
    if (!dev_) return -1;
    std::string key = upper(name);
    if (auto it = byName_.find(key); it != byName_.end()) return it->second;
    int id = -1;
    std::vector<uint8_t> file;
    // plain names live in DATA.SFX\SOUND; "SPEAK_E\..." paths (speech) are relative to DATA.SFX
    if (fs_->read(key.find('\\') != std::string::npos ? "DATA.SFX\\" + key : "DATA.SFX\\SOUND\\" + key, file)) {
        SDL_AudioSpec spec; Uint8* buf = nullptr; Uint32 len = 0;
        if (SDL_LoadWAV_RW(SDL_RWFromConstMem(file.data(), int(file.size())), 1, &spec, &buf, &len)) {
            SDL_AudioCVT cvt;
            if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 1, rate_) >= 0) {
                std::vector<uint8_t> tmp(size_t(len) * size_t(std::max(1, cvt.len_mult)));
                std::copy(buf, buf + len, tmp.begin());
                cvt.buf = tmp.data(); cvt.len = int(len);
                if (cvt.needed) SDL_ConvertAudio(&cvt);
                Sample s;
                s.pcm.resize(size_t(cvt.needed ? cvt.len_cvt : int(len)) / 2);
                std::memcpy(s.pcm.data(), tmp.data(), s.pcm.size() * 2);
                auto v = volumes_.find(key);
                s.baseVolume = v != volumes_.end() ? float(v->second) / 100.f : 1.f;
                SDL_LockAudioDevice(dev_);
                samples_.push_back(std::move(s));
                id = int(samples_.size()) - 1;
                SDL_UnlockAudioDevice(dev_);
            }
            SDL_FreeWAV(buf);
        }
    }
    byName_[key] = id;
    return id;
}

int Audio::play(int sound, float volume, float pan, bool loop, float pitch) {
    if (!dev_ || sound < 0) return 0;
    SDL_LockAudioDevice(dev_);
    // free voice, otherwise steal the quietest non-looping one
    Voice* v = nullptr;
    for (auto& x : voices_) if (x.sample < 0) { v = &x; break; }
    if (!v) for (auto& x : voices_) if (!x.loop && (!v || x.vol < v->vol)) v = &x;
    int id = 0;
    if (v) {
        *v = Voice{sound, 0.0, volume, std::clamp(pan, -1.f, 1.f), pitch, loop, nextId_++};
        id = v->id;
    }
    SDL_UnlockAudioDevice(dev_);
    return id;
}

int Audio::playAt(int sound, Vec3 pos, float volume, float pitch) {
    Vec3 d = pos - listener_;
    float dist = std::sqrt(dot(d, d));
    float att = 1.f / (1.f + dist / 120.f);
    if (att < 0.03f) return 0;
    float pan = dist > 1e-3f ? dot(d, listenerRight_) / dist * 0.7f : 0.f;
    return play(sound, volume * att, pan, false, pitch);
}

static void posParams(Vec3 d, Vec3 right, float& att, float& pan) {
    float dist = std::sqrt(dot(d, d));
    att = 1.f / (1.f + dist / 120.f);
    pan = dist > 1e-3f ? dot(d, right) / dist * 0.7f : 0.f;
}

int Audio::playLoopAt(int sound, Vec3 pos, float volume) {
    float att, pan; posParams(pos - listener_, listenerRight_, att, pan);
    return play(sound, volume * att, pan, true);
}

void Audio::setVoicePos(int voice, Vec3 pos, float volume) {
    if (!dev_ || !voice) return;
    float att, pan; posParams(pos - listener_, listenerRight_, att, pan);
    SDL_LockAudioDevice(dev_);
    for (auto& v : voices_) if (v.id == voice && v.sample >= 0) { v.vol = volume * att; v.pan = std::clamp(pan, -1.f, 1.f); }
    SDL_UnlockAudioDevice(dev_);
}

void Audio::setVoice(int voice, float volume, float pitch) {
    if (!dev_ || !voice) return;
    SDL_LockAudioDevice(dev_);
    for (auto& v : voices_) if (v.id == voice && v.sample >= 0) { v.vol = volume; if (pitch > 0) v.pitch = pitch; }
    SDL_UnlockAudioDevice(dev_);
}

bool Audio::playing(int voice) {
    if (!dev_ || !voice) return false;
    SDL_LockAudioDevice(dev_);
    bool on = false;
    for (auto& v : voices_) if (v.id == voice && v.sample >= 0) on = true;
    SDL_UnlockAudioDevice(dev_);
    return on;
}

void Audio::stop(int voice) {
    if (!dev_ || !voice) return;
    SDL_LockAudioDevice(dev_);
    for (auto& v : voices_) if (v.id == voice) v.sample = -1;
    SDL_UnlockAudioDevice(dev_);
}

void Audio::callback(void* user, Uint8* stream, int len) {
    static_cast<Audio*>(user)->mix(reinterpret_cast<int16_t*>(stream), len / 4);
}

void Audio::mix(int16_t* out, int frames) {
    std::vector<float> acc(size_t(frames) * 2, 0.f);
    for (auto& v : voices_) {
        if (v.sample < 0) continue;
        const Sample& s = samples_[size_t(v.sample)];
        if (s.pcm.empty()) { v.sample = -1; continue; }
        float g = v.vol * s.baseVolume * masterVolume;
        float gl = g * std::min(1.f, 1.f - v.pan), gr = g * std::min(1.f, 1.f + v.pan);
        for (int i = 0; i < frames; i++) {
            size_t p = size_t(v.pos);
            if (p >= s.pcm.size()) {
                if (v.loop) { v.pos = std::fmod(v.pos, double(s.pcm.size())); p = size_t(v.pos); }
                else { v.sample = -1; break; }
            }
            float x = s.pcm[p];
            acc[size_t(i) * 2] += x * gl; acc[size_t(i) * 2 + 1] += x * gr;
            v.pos += v.pitch;
        }
    }
    if (music_) {
        const double step = double(musicRate_) / double(rate_);
        const float g = musicVolume * masterVolume * 32767.f;
        for (int i = 0; i < frames && music_; i++) {
            while (musicPos_ + 1.0 >= double(musicFrames_)) {
                // keep the last frame so interpolation stays continuous across chunks
                float lastL = 0, lastR = 0;
                if (musicFrames_ > 0) { lastL = musicChunk_[(musicFrames_ - 1) * 2]; lastR = musicChunk_[(musicFrames_ - 1) * 2 + 1]; }
                double carry = musicPos_ - double(musicFrames_ > 0 ? musicFrames_ - 1 : 0);
                musicChunk_.resize(2 * 2049);
                musicChunk_[0] = lastL; musicChunk_[1] = lastR;
                int got;
                if (musicChannels_ >= 2) got = stb_vorbis_get_samples_float_interleaved(music_, 2, musicChunk_.data() + 2, 2048 * 2);
                else {
                    got = stb_vorbis_get_samples_float_interleaved(music_, 1, musicChunk_.data() + 2, 2048);
                    for (int k = got - 1; k >= 0; k--) { musicChunk_[2 + 2 * size_t(k) + 1] = musicChunk_[2 + size_t(k)]; musicChunk_[2 + 2 * size_t(k)] = musicChunk_[2 + size_t(k)]; }
                }
                if (got <= 0) {
                    if (musicLoop_) { stb_vorbis_seek_start(music_); continue; }
                    stb_vorbis_close(music_); music_ = nullptr; break;
                }
                musicFrames_ = size_t(got) + 1;
                musicPos_ = carry < 0 ? 0 : carry;
            }
            if (!music_) break;
            size_t p = size_t(musicPos_);
            float f = float(musicPos_ - double(p));
            float l = musicChunk_[p * 2] * (1 - f) + musicChunk_[(p + 1) * 2] * f;
            float r = musicChunk_[p * 2 + 1] * (1 - f) + musicChunk_[(p + 1) * 2 + 1] * f;
            acc[size_t(i) * 2] += l * g; acc[size_t(i) * 2 + 1] += r * g;
            musicPos_ += step;
        }
    }
    for (size_t i = 0; i < acc.size(); i++) out[i] = int16_t(std::clamp(acc[i], -32768.f, 32767.f));
}

} // namespace xa
