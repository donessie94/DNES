#include "../include/SDLScreen.h"

bool SDLScreen::create_texture(SDL_Texture*& texture, int w, int h)
{
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        w,
        h
    );

    if (!texture) {
        last_error_message = SDL_GetError();
        return false;
    }

    return true;
}

bool SDLScreen::initialize(int w, int h, int s)
{
    width = w;
    height = h;
    scale = s;

    screen_w_px = width * scale;
    screen_h_px = height * scale;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        last_error_message = SDL_GetError();
        return false;
    }

    // DEBUG TEXTURES LOGICAL SIZE
    constexpr int palette_w = 128;
    constexpr int palette_h = 64;

    constexpr int pattern_w = 256; // 256
    constexpr int pattern_h = 128; // 128

    constexpr int nametable_w = 256;
    constexpr int nametable_h = 240;

    // Window layout
    // Left: main NES screen
    // Right: stacked debug panels
    constexpr float gap = 16.0f;
    constexpr float sidebar_panel_w = 512.0f; // debug area width

    // Preserving the aspect ratio. Width is fixed for all of them, so
    // we need to choose height so the image is not stretched
    // Derivation: NH/NW = H/W ==> NH = NW * H/W
    // new_height = new_width * H / W
    const float palette_h_px =
        sidebar_panel_w * static_cast<float>(palette_h) / static_cast<float>(palette_w);

    const float pattern_h_px =
        sidebar_panel_w * static_cast<float>(pattern_h) / static_cast<float>(pattern_w);

    const float nametable_h_px =
        sidebar_panel_w * static_cast<float>(nametable_h) / static_cast<float>(nametable_w);

    const float sidebar_w = sidebar_panel_w;
    const float sidebar_h =
        palette_h_px + gap +
        pattern_h_px + gap +
        nametable_h_px;

    const int window_w = static_cast<int>(screen_w_px + gap + sidebar_w);
    const int window_h = static_cast<int>(
        (static_cast<float>(screen_h_px) > sidebar_h) ? screen_h_px : sidebar_h
    );

    window = SDL_CreateWindow("DNES", window_w, window_h, 0);
    if (!window) {
        last_error_message = SDL_GetError();
        SDL_Quit();
        return false;
    }

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        last_error_message = SDL_GetError();
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return false;
    }

    if (!create_texture(screen_texture, width, height)) {
        shutdown();
        return false;
    }

    if (!create_texture(palette_texture, palette_w, palette_h)) {
        shutdown();
        return false;
    }

    if (!create_texture(pattern_texture, pattern_w, pattern_h)) {
        shutdown();
        return false;
    }

    if (!create_texture(nametable_texture, nametable_w, nametable_h)) {
        shutdown();
        return false;
    }

    // DESTINATION REGIONS ON SCREEN
    screen_region = {
        .x = 0.0f,
        .y = 0.0f,
        .w = static_cast<float>(screen_w_px),
        .h = static_cast<float>(screen_h_px)
    };

    const float sidebar_x = static_cast<float>(screen_w_px) + gap;
    float sidebar_y = 0.0f;

    palette_region = {
        .x = sidebar_x,
        .y = sidebar_y,
        .w = sidebar_panel_w,
        .h = palette_h_px
    };
    sidebar_y += palette_h_px + gap;

    pattern_region = {
        .x = sidebar_x,
        .y = sidebar_y,
        .w = sidebar_panel_w,
        .h = pattern_h_px
    };
    sidebar_y += pattern_h_px + gap;

    nametable_region = {
        .x = sidebar_x,
        .y = sidebar_y,
        .w = sidebar_panel_w,
        .h = nametable_h_px
    };

    return true;
}

void SDLScreen::shutdown()
{
    if (screen_texture) {
        SDL_DestroyTexture(screen_texture);
        screen_texture = nullptr;
    }

    if (palette_texture) {
        SDL_DestroyTexture(palette_texture);
        palette_texture = nullptr;
    }

    if (pattern_texture) {
        SDL_DestroyTexture(pattern_texture);
        pattern_texture = nullptr;
    }

    if (nametable_texture) {
        SDL_DestroyTexture(nametable_texture);
        nametable_texture = nullptr;
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

void SDLScreen::update_screen_texture(const FrameBuffer& frame_buffer, int pitch)
{
    SDL_UpdateTexture(screen_texture, nullptr, frame_buffer.data(), pitch);
}

void SDLScreen::update_palette_texture(const FrameBuffer& frame_buffer, int pitch)
{
    SDL_UpdateTexture(palette_texture, nullptr, frame_buffer.data(), pitch);
}

void SDLScreen::update_pattern_texture(const FrameBuffer& frame_buffer, int pitch)
{
    SDL_UpdateTexture(pattern_texture, nullptr, frame_buffer.data(), pitch);
}

void SDLScreen::update_nametable_texture(const FrameBuffer& frame_buffer, int pitch)
{
    SDL_UpdateTexture(nametable_texture, nullptr, frame_buffer.data(), pitch);
}

void SDLScreen::present_frame()
{
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
    SDL_RenderClear(renderer);

    SDL_RenderTexture(renderer, screen_texture, nullptr, &screen_region);
    SDL_RenderTexture(renderer, palette_texture, nullptr, &palette_region);
    SDL_RenderTexture(renderer, pattern_texture, nullptr, &pattern_region);
    SDL_RenderTexture(renderer, nametable_texture, nullptr, &nametable_region);

    SDL_RenderPresent(renderer);
}