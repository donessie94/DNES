# DNES

DNES is a simple NES emulator written in C++.

## Overview

This project focuses on building the emulator core from the ground up, with an emphasis on clean architecture and understanding the NES at the hardware level. The current work includes CPU emulation, cartridge loading, mapper support, early PPU implementation, and SDL3 integration for visual debugging and display output.

## Current Features

- 6502 CPU emulation in progress
- iNES ROM loading
- Mapper 0 / NROM support
- Cartridge PRG/CHR access
- PPU register read/write handling
- PPU internal memory routing
- OAM DMA support
- SDL3 integrated through a git submodule

## Project Structure

- `include/` — header files
- `src/` — source files
- `ROMs/` — local ROM folder (ignored by git)
- `third_party/SDL/` — SDL3 git submodule
- `build/` — local build output

## Build Instructions

```bash
git clone <https://github.com/donessie94/DNES>
cd DNES
git submodule update --init --recursive
cmake -S . -B build
cmake --build build
./build/dnes
```