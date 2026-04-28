#include <iostream>
#include <chrono>
#include <thread>
#include "../include/nes.h"
#include "../include/SDLScreen.h"

int main()
{
    using Clock = std::chrono::steady_clock;
    constexpr auto target_frame_time = std::chrono::microseconds(16667);

    if(!SDL_Init(SDL_INIT_VIDEO)){
        std::cerr << SDL_GetError();
        return 1;
    }

    SDLScreen sdl_screen;
    if(!sdl_screen.initialize(256, 240, 4, false)){
        std::cerr << sdl_screen.get_error();
        SDL_Quit();
        return 1;
    }

    NES nes;
    if(!nes.cartridge.initialize_from_file("ROMs/Super_Mario_Bros.nes")){
        std::cout << "Error loading ROM" << '\n';
        SDL_Quit();
        return 1;
    }

    nes.cpu.reset();
    nes.ppu.current_dot = -1;
    nes.ppu.frame_ready = false;

    bool running = true;
    while(running){

        auto frame_start_time = Clock::now();

        SDL_Event event;
        while(SDL_PollEvent(&event)){
            if(event.type == SDL_EVENT_QUIT){
                running = false;
            }

            if(event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat){
                SDL_Keycode key = event.key.key;

                if(key == SDLK_Z){
                    nes.bus.controller1_ref->set_button(Controller::Button::A, true);
                }
                else if(key == SDLK_X){
                    nes.bus.controller1_ref->set_button(Controller::Button::B, true);
                }
                else if(key == SDLK_RSHIFT){
                    nes.bus.controller1_ref->set_button(Controller::Button::Select, true);
                }
                else if(key == SDLK_RETURN){
                    nes.bus.controller1_ref->set_button(Controller::Button::Start, true);
                }
                else if(key == SDLK_UP){
                    nes.bus.controller1_ref->set_button(Controller::Button::Up, true);
                }
                else if(key == SDLK_DOWN){
                    nes.bus.controller1_ref->set_button(Controller::Button::Down, true);
                }
                else if(key == SDLK_LEFT){
                    nes.bus.controller1_ref->set_button(Controller::Button::Left, true);
                }
                else if(key == SDLK_RIGHT){
                    nes.bus.controller1_ref->set_button(Controller::Button::Right, true);
                }
            }

            if(event.type == SDL_EVENT_KEY_UP){
                SDL_Keycode key = event.key.key;

                if(key == SDLK_Z){
                    nes.bus.controller1_ref->set_button(Controller::Button::A, false);
                }
                else if(key == SDLK_X){
                    nes.bus.controller1_ref->set_button(Controller::Button::B, false);
                }
                else if(key == SDLK_RSHIFT){
                    nes.bus.controller1_ref->set_button(Controller::Button::Select, false);
                }
                else if(key == SDLK_RETURN){
                    nes.bus.controller1_ref->set_button(Controller::Button::Start, false);
                }
                else if(key == SDLK_UP){
                    nes.bus.controller1_ref->set_button(Controller::Button::Up, false);
                }
                else if(key == SDLK_DOWN){
                    nes.bus.controller1_ref->set_button(Controller::Button::Down, false);
                }
                else if(key == SDLK_LEFT){
                    nes.bus.controller1_ref->set_button(Controller::Button::Left, false);
                }
                else if(key == SDLK_RIGHT){
                    nes.bus.controller1_ref->set_button(Controller::Button::Right, false);
                }
            }
        }

        while(!nes.ppu.frame_ready){
            int cpu_cycles_taken = nes.cpu.exec_nxt_instr();

            int ppu_cycles_to_advance = 3 * cpu_cycles_taken;
            while(ppu_cycles_to_advance > 0){
                nes.ppu.step_one_cycle();
                ppu_cycles_to_advance--;

                if(nes.ppu.NMI_request == true){
                    nes.cpu.handle_nmi_interrupt();
                    nes.ppu.NMI_request = false;
                }

                if(nes.ppu.frame_ready){
                    break;
                }
            }
        }

        sdl_screen.update_screen_texture(nes.ppu.screen_pixels, 256 * 4);
        sdl_screen.present_frame();

        nes.ppu.frame_ready = false;

        auto frame_end_time = Clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(
            frame_end_time - frame_start_time);

        if(elapsed_time < target_frame_time){
            std::this_thread::sleep_for(target_frame_time - elapsed_time);
        }
    }

    SDL_Quit();
    return 0;
}