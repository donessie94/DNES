#pragma once
#include <SDL3/SDL.h>
#include <string>

class APU;

class SDLAudio {
public:
    SDLAudio() = default;
    ~SDLAudio();

    bool initialize(int sample_rate = 44100);
    void shutdown();

    std::string get_error() const;

private:
    SDL_AudioDeviceID device_id_{0};
    SDL_AudioStream* stream_{nullptr};
    int sample_rate_{44100};
    std::string error_message_{};
};