#include "LR35902.h"
#include "PPU.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr int GB_WIDTH = PPU_LR35902::WIDTH;
    constexpr int GB_HEIGHT = PPU_LR35902::HEIGHT;
    constexpr int SCALE = 4;
    constexpr int CYCLES_PER_FRAME = 70224;

    void print_usage(const char *program_name)
    {
        std::cout << "Usage: " << program_name << " [ROM_PATH]\n"
                  << "\n"
                  << "Run the LR35902 emulator with either a Game Boy ROM or the built-in demo program.\n"
                  << "\n"
                  << "Examples:\n"
                  << "  " << program_name << "\n"
                  << "  " << program_name << " /path/to/game.gb\n"
                  << "  " << program_name << " --help\n";
    }

    void initialize_demo_vram(CPU_LR35902 &cpu)
    {
        std::fill(cpu.memory.begin(), cpu.memory.end(), 0);
        cpu.memory[0xFF40] = 0x91;
        cpu.memory[0xFF47] = 0xE4;

        for (int tile = 0; tile < 32; ++tile)
        {
            for (int row = 0; row < 8; ++row)
            {
                uint8_t low = 0;
                uint8_t high = 0;

                for (int bit = 0; bit < 8; ++bit)
                {
                    const int pattern = ((tile + row + bit) % 4);
                    const int bit_index = 7 - bit;

                    if ((pattern & 0x01) != 0)
                    {
                        low |= static_cast<uint8_t>(1u << bit_index);
                    }
                    if ((pattern & 0x02) != 0)
                    {
                        high |= static_cast<uint8_t>(1u << bit_index);
                    }
                }

                const uint16_t tile_addr = 0x8000 + static_cast<uint16_t>(tile * 16 + row * 2);
                cpu.memory[tile_addr] = low;
                cpu.memory[tile_addr + 1] = high;
            }
        }

        for (int tile_y = 0; tile_y < 18; ++tile_y)
        {
            for (int tile_x = 0; tile_x < 20; ++tile_x)
            {
                const uint8_t tile_id = static_cast<uint8_t>((tile_x + tile_y) % 32);
                cpu.memory[0x9800 + static_cast<uint16_t>(tile_y * 32 + tile_x)] = tile_id;
            }
        }
    }

    void print_cpu_state(const CPU_LR35902 &cpu)
    {
        std::cout << "PC=0x" << std::hex << cpu.pc << "  "
                  << "SP=0x" << cpu.sp << "  "
                  << "AF=0x" << ((cpu.af.reg16 & 0xFFFFu) >> 0) << "  "
                  << "BC=0x" << cpu.bc.reg16 << "  "
                  << "DE=0x" << cpu.de.reg16 << "  "
                  << "HL=0x" << cpu.hl.reg16 << "\n";
        std::cout << "A=0x" << std::hex << static_cast<int>(cpu.af.reg8.high)
                  << " F=0x" << static_cast<int>(cpu.af.reg8.low)
                  << " Z=" << get_flag(cpu, FLAG_Z)
                  << " N=" << get_flag(cpu, FLAG_N)
                  << " H=" << get_flag(cpu, FLAG_H)
                  << " C=" << get_flag(cpu, FLAG_C)
                  << "\n";
    }

    void load_demo_program(CPU_LR35902 &cpu)
    {
        cpu.pc = 0x0100;
        const std::vector<uint8_t> program = {
            0x3E, 0x42, // LD A, 0x42
            0x06, 0x05, // LD B, 0x05
            0x80,       // ADD A, B
            0x76        // HALT
        };

        for (std::size_t i = 0; i < program.size(); ++i)
        {
            cpu.memory[cpu.pc + static_cast<uint16_t>(i)] = program[i];
        }
    }

    bool load_rom(CPU_LR35902 &cpu, const std::filesystem::path &path, std::string &error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "could not open file.";
            return false;
        }

        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return load_rom_only_cartridge(cpu, std::move(rom), error);
    }
}

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    if (argc == 2)
    {
        const std::string arg = argv[1];
        if (arg == "-h" || arg == "--help")
        {
            print_usage(argv[0]);
            return 0;
        }
    }

    CPU_LR35902 cpu{};

    if (argc == 2)
    {
        const std::filesystem::path rom_path(argv[1]);
        std::string rom_error;
        if (!load_rom(cpu, rom_path, rom_error))
        {
            std::cerr << "Unable to load ROM: " << rom_path << " (" << rom_error << ")\n\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    else
    {
        initialize_demo_vram(cpu);
        load_demo_program(cpu);
        std::cout << "LR35902 emulator\n";
        std::cout << "Executing a short demo program.\n\n";
    }

    if (argc == 2)
    {
        std::cout << "LR35902 emulator\n";
        std::cout << "Loading ROM: " << argv[1] << "\n\n";
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    const int windowWidth = GB_WIDTH * SCALE;
    const int windowHeight = GB_HEIGHT * SCALE;

    SDL_Window *window =
        SDL_CreateWindow("LR35902 Emu",
                         SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED,
                         windowWidth,
                         windowHeight,
                         SDL_WINDOW_SHOWN);
    if (!window)
    {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        std::cerr << "Renderer creation failed (" << SDL_GetError() << "), trying software."
                  << std::endl;
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer)
    {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_RendererInfo renderer_info{};
    const bool renderer_vsync = SDL_GetRendererInfo(renderer, &renderer_info) == 0 &&
                                (renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STREAMING, GB_WIDTH,
                                             GB_HEIGHT);
    if (!texture)
    {
        std::cerr << "Texture creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    PPU_LR35902 ppu{};
    bool running = true;
    int cycle_accumulator = 0;

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN)
            {
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    running = false;
                }
            }
        }

        while (running && cycle_accumulator < CYCLES_PER_FRAME)
        {
            const int cycles = step_cpu(cpu);
            update_ppu(cpu, ppu, cycles);
            cycle_accumulator += cycles;
        }
        if (!running)
            break;
        cycle_accumulator -= CYCLES_PER_FRAME;

        SDL_UpdateTexture(texture, nullptr, ppu.framebuffer.data(), GB_WIDTH * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        if (!renderer_vsync)
        {
            SDL_Delay(16);
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Final CPU state:\n";
    print_cpu_state(cpu);
    return 0;
}
