// void run_nesTest()
// {
//     NES nes;
//     std::string nestest_path = "ROMs/nestest.nes";
//     if(!nes.cartridge.initialize_from_file(nestest_path)){
//         std::cout<<"Error loading ROM"<<'\n';
//         return;
//     }

//     std::ofstream log_file("nestest_mylog.txt");
//     nes.cpu.reset();
//     nes.cpu.regs.pc = 0xC000;
//     nes.cpu.total_cpu_cycles = 7;
//     nes.cpu.run_test_mode(std::cout, log_file, 8199);
// }

// int test_sdl()
// {
//     if (!SDL_Init(SDL_INIT_VIDEO)) {
//         std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
//         return 1;
//     }
//     SDL_Window* window = SDL_CreateWindow(
//         "DNES",
//         800,
//         600,
//         0
//     );
//     if (!window) {
//         std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
//         SDL_Quit();
//         return 1;
//     }
//     SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
//     if (!renderer) {
//         std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
//         SDL_DestroyWindow(window);
//         SDL_Quit();
//         return 1;
//     }
//     bool running = true;
//     while (running) {
//         SDL_Event event;
//         while (SDL_PollEvent(&event)) {
//             if (event.type == SDL_EVENT_QUIT) {
//                 running = false;
//             }
//         }
//         SDL_SetRenderDrawColor(renderer, 20, 40, 80, 255);
//         SDL_RenderClear(renderer);
//         SDL_RenderPresent(renderer);
//     }
//     SDL_DestroyRenderer(renderer);
//     SDL_DestroyWindow(window);
//     SDL_Quit();
//     return 0;
// }
