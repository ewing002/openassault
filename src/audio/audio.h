#pragma once
#include "../core/fs.h"
#include "../render/math.h"
#include <SDL.h>
#include <string>
#include <vector>
#include <map>

struct stb_vorbis;
namespace xa {

// Small software mixer for the game's WAV samples (DATA.SFX\SOUND, volumes from SAMPLES.SET).
class Audio {
public:
    bool init(const GameFS& fs);
    void shutdown();
    int sound(const std::string& name);              // load on demand, returns id or -1
    int play(int sound, float volume = 1.f, float pan = 0.f, bool loop = false, float pitch = 1.f);
    // Positional helper: attenuates with distance from the listener.
    int playAt(int sound, Vec3 pos, float volume = 1.f, float pitch = 1.f);
    void setVoice(int voice, float volume, float pitch = -1.f);
    void setVoicePos(int voice, Vec3 pos, float volume);   // looping positional voices
    int playLoopAt(int sound, Vec3 pos, float volume);
    void stop(int voice);
    bool playing(int voice);                         // the voice is still sounding
    void setListener(Vec3 pos, Vec3 right) { listener_ = pos; listenerRight_ = right; }
    // Streaming background music (Ogg Vorbis, e.g. the CD tracks in MUSIC\track_NN.ogg)
    bool playMusic(const std::string& path, bool loop = true);
    void stopMusic();
    float musicVolume = 0.55f;
    float masterVolume = 0.8f;
    bool ok() const { return dev_ != 0; }
private:
    struct Sample { std::vector<int16_t> pcm; float baseVolume = 1.f; };
    struct Voice { int sample = -1; double pos = 0; float vol = 0, pan = 0, pitch = 1; bool loop = false; int id = 0; };
    static void callback(void* user, Uint8* stream, int len);
    void mix(int16_t* out, int frames);
    const GameFS* fs_ = nullptr;
    SDL_AudioDeviceID dev_ = 0;
    int rate_ = 22050;
    std::vector<Sample> samples_;
    std::map<std::string, int> byName_;
    std::map<std::string, int> volumes_;
    std::vector<Voice> voices_;
    int nextId_ = 1;
    Vec3 listener_, listenerRight_{1, 0, 0};
    // music state (touched by the audio callback; guarded by SDL_LockAudioDevice)
    std::vector<uint8_t> musicData_;
    ::stb_vorbis* music_ = nullptr;
    int musicChannels_ = 2, musicRate_ = 44100;
    bool musicLoop_ = true;
    std::vector<float> musicChunk_;
    size_t musicFrames_ = 0;
    double musicPos_ = 0;
};

} // namespace xa
