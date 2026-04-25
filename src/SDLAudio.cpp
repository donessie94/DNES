#include "../include/SDLAudio.h"
#include "../include/apu.h"
#include <algorithm>
#include <vector>

SDLAudio::~SDLAudio()
{
    shutdown();
}

bool SDLAudio::initialize(int sample_rate)
{
    sample_rate_ = sample_rate;

    SDL_AudioSpec desired_spec{};
    desired_spec.format = SDL_AUDIO_F32;
    desired_spec.channels = 1;
    desired_spec.freq = sample_rate_;

    device_id_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec);
    if(device_id_ == 0){
        error_message_ = SDL_GetError();
        return false;
    }

    stream_ = SDL_CreateAudioStream(&desired_spec, &desired_spec);
    if(stream_ == nullptr){
        error_message_ = SDL_GetError();
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
        return false;
    }

    if(!SDL_BindAudioStream(device_id_, stream_)){
        error_message_ = SDL_GetError();
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
        return false;
    }

    if(!SDL_ResumeAudioDevice(device_id_)){
        error_message_ = SDL_GetError();
        shutdown();
        return false;
    }

    return true;
}

void SDLAudio::shutdown()
{
    if(stream_ != nullptr){
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }

    if(device_id_ != 0){
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
    }
}

std::string SDLAudio::get_error() const
{
    return error_message_;
}