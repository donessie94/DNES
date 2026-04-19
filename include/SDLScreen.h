#pragma once
#include"types.h"
#include <SDL3/SDL.h>

class SDLScreen {
public:
    SDLScreen() = default;
    ~SDLScreen() { shutdown(); }
    const char* get_error() const { return last_error_message; }
    bool initialize(int w = 256, int h = 240, int s = 4);
    void shutdown();
    void update_screen_texture(const FrameBuffer& frame_buffer, int pitch);
    void update_palette_texture(const FrameBuffer& frame_buffer, int pitch);
    void update_pattern_texture(const FrameBuffer& frame_buffer, int pitch);
    void update_nametable_texture(const FrameBuffer& frame_buffer, int pitch);
    void present_frame();
private:
    bool create_texture(SDL_Texture*& texture, int w, int h);
    const char* last_error_message{};
    int width{}; // actual width of screen texture (logical image)
    int height{};//
    int scale{};
    int screen_w_px{};
    int screen_h_px{};
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* screen_texture{};
    SDL_Texture* palette_texture{};
    SDL_Texture* nametable_texture{};
    SDL_Texture* pattern_texture{};
    SDL_FRect screen_region{};
    SDL_FRect palette_region{};
    SDL_FRect pattern_region{};
    SDL_FRect nametable_region{};
};