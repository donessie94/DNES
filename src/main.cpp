#include <iostream>
#include "../include/nes.h"

int main(){
    
    NES nes;
    std::string nestest_path = "ROMs/nestest.nes";
    if(!nes.cartridge.initialize_from_file(nestest_path)){
        std::cout<<"Error loading ROM"<<'\n';
        return;
    }

    std::ofstream log_file("nestest_mylog.txt");
    nes.cpu.reset();
    nes.cpu.regs.pc = 0xC000;
    nes.cpu.total_cpu_cycles = 7;
    nes.cpu.run_test_mode(std::cout, log_file, 8199);

    return 0;
}