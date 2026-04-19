#include <iostream>
#include "../include/nes.h"
#include "../include/SDLScreen.h"

int main()
{
    SDLScreen sdl_screen;
    if(!sdl_screen.initialize(256, 240, 4)){
        std::cerr<<sdl_screen.get_error();
        return 1;
    }

    NES nes;
    if(!nes.cartridge.initialize_from_file("ROMs/donkey_kong.nes")){
        std::cout<<"Error loading ROM"<<'\n';
        return 1;
    }

    nes.cpu.reset();

    bool running = true;
    while (running) {

        int instructions = 1000;
        while (instructions > 0){   // 1000 instr at a time for now
            int cpu_cycles_taken = nes.cpu.exec_nxt_instr();

            // standard estimate, PPU is 3x faster than CPU
            int ppu_cycles_to_advance = 3 * cpu_cycles_taken;

            while(ppu_cycles_to_advance > 0){
                nes.ppu.step_one_cycle();
                ppu_cycles_to_advance--;
            }

            if(nes.ppu.NMI_request == true){
                nes.cpu.handle_nmi_interrupt();
                nes.ppu.NMI_request = false;
            }

            instructions--;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        DebugImage palette_img   = nes.ppu.build_palette_debug_image();
        DebugImage pattern_img   = nes.ppu.build_pattern_table_debug_image();
        DebugImage nametable_img = nes.ppu.build_nametable_debug_image();

        // Pitch is how many Bytes in a row
        sdl_screen.update_palette_texture(palette_img.pixels, palette_img.width * 4);
        sdl_screen.update_pattern_texture(pattern_img.pixels, pattern_img.width * 4);
        sdl_screen.update_nametable_texture(nametable_img.pixels, nametable_img.width * 4);
        sdl_screen.update_screen_texture(nes.ppu.screen_pixels, 256 * 4);

        sdl_screen.present_frame();
    }

    return 0;
}