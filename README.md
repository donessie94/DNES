# DNES

DNES is a NES emulator project written in modern C++ with SDL3 for graphics/input and CMake for building.

The goal of this project is to understand emulator architecture by implementing the major NES subsystems step by step rather than treating the emulator like a black box.

## Screenshots
<img width="1023" height="989" alt="Screenshot 2026-04-27 at 9 43 29 PM" src="https://github.com/user-attachments/assets/b7e61092-afe5-479b-bd3b-d21f1fcb7f03" />
<img width="1025" height="991" alt="Screenshot 2026-04-27 at 9 44 29 PM" src="https://github.com/user-attachments/assets/e0ea65cb-ff3e-4002-a236-ae0ff183ae4f" />
<img width="1023" height="986" alt="Screenshot 2026-04-27 at 9 42 28 PM" src="https://github.com/user-attachments/assets/c083794b-6a1f-4382-bc8f-d4dc0c565e4d" />


## Current state

The project currently has working or partially working support for:

- 6502 CPU emulation
- cartridge loading and mapper handling
- PPU rendering
- controller input
- frame-based execution loop
- SDL window output

At this point, games can run and render, and controller input is wired in.

## What has been implemented

### CPU
The project includes a custom 6502 CPU implementation with instruction execution, tracing support, and bus integration. All official (legal) 6502 instructions are implemented, while undocumented / illegal opcodes are not currently supported.

### Cartridge / Mapper
The emulator loads `.nes` ROMs, parses iNES header information, and routes CPU/PPU memory accesses through cartridge/mapper logic.

### PPU
The PPU has been implemented far enough to render game graphics on screen. This includes:

- background fetching pipeline
- sprite rendering
- sprite zero hit handling
- nametable mirroring support
- frame-ready execution flow
- NMI triggering during vblank

The PPU is the most complete subsystem at the moment.

### Controller input
A controller class has been added and connected through the bus. Keyboard input is mapped to controller buttons through SDL events.

### SDL / frontend
The emulator uses SDL3 for:

- creating the application window
- rendering the screen texture
- polling keyboard input

## What is still missing / incomplete

The project is still under development. Important missing or incomplete areas include:

### APU / audio
Audio is not implemented yet in a finished form.

There is placeholder/scaffolding work for audio-related classes, but proper NES APU emulation is still planned work.

### Accuracy / timing polish
Although games run, the emulator is still not aiming for perfect cycle-accurate behavior yet. Timing and hardware quirks may still need refinement.

### Compatibility
Compatibility is currently limited. ROM support depends heavily on the features and timing assumptions of each game, and at the moment the emulator is only expected to work reliably with ROMs that use the currently implemented mapper.

### Mapper support
At present, only **Mapper 0 (NROM)** is implemented.

Support for additional mappers is planned for future development, which will improve compatibility across a wider range of NES titles.

### Emulator polish
Potential future work includes:

- better timing accuracy
- audio implementation
- debugging tools
- save states
- improved UI / menu support
- more robust ROM compatibility

## Controls

Current keyboard mapping:

- `Z` = A
- `X` = B
- `Right Shift` = Select
- `Enter` = Start
- Arrow keys = D-pad

## Build requirements

You need:

- a C++20 compiler
- CMake 3.20 or newer
- Git
- the project submodules initialized

SDL3 is included through the `third_party/SDL` submodule, so you do not need a separate system-wide SDL install if building this repository as-is.

## Clone the repository

```bash
git clone https://github.com/donessie94/DNES
cd DNES
git submodule update --init --recursive
```

## Build Instructions

## Build and run on Linux

```bash
cmake -S . -B build
cmake --build build
./build/dnes
```

## Build and run on Windows

```PowerShell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\dnes.exe
```

## Notes on cross-platform support

The project is designed to be cross-platform because it uses:

- standard C++
- CMake
- SDL3

However, helper scripts such as `run.sh` are Unix-oriented and are not intended for Windows. On Windows, use the CMake commands directly.

## Project structure

A simplified overview:

- `src/` -> implementation files
- `include/` -> headers
- `third_party/SDL/` -> SDL3 submodule
- `ROMs/` -> local ROM files for testing
- `CMakeLists.txt` -> build configuration

## Development philosophy

This project is being built as a learning-oriented emulator project. The focus is on:

- understanding how the NES hardware works
- implementing each subsystem by hand
- keeping the code readable and educational
- improving accuracy over time

## Disclaimer

This project is for educational purposes. You should only use ROMs that you legally own or are otherwise allowed to use.

## Future plans

Planned future improvements include:

- proper APU/audio implementation
- more mapper support
- timing cleanup and accuracy improvements
- better debugging / visualization tools
- possible save-state support

---

This emulator is still in progress, but the core foundation is already in place: CPU, cartridge handling, PPU rendering, controller input, and frame-based execution.
