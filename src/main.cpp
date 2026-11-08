#include "LR35902.h"
#include "PPU.h"
#include "SaveState.h"

#include <SDL2/SDL.h>

#include <algorithm>
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
    // Frames run per displayed frame while fast-forwarding.
    constexpr int FAST_FORWARD_FRAMES = 4;

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
        cpu.memory[0xFF00] = 0xCF;
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
        return load_cartridge(cpu, std::move(rom), error);
    }

    void update_joypad_from_key(CPU_LR35902 &cpu, SDL_Keycode key, bool pressed)
    {
        switch (key)
        {
        case SDLK_RIGHT:
            cpu_set_joypad_button(cpu, JoypadButton::Right, pressed);
            break;
        case SDLK_LEFT:
            cpu_set_joypad_button(cpu, JoypadButton::Left, pressed);
            break;
        case SDLK_UP:
            cpu_set_joypad_button(cpu, JoypadButton::Up, pressed);
            break;
        case SDLK_DOWN:
            cpu_set_joypad_button(cpu, JoypadButton::Down, pressed);
            break;
        case SDLK_z:
            cpu_set_joypad_button(cpu, JoypadButton::A, pressed);
            break;
        case SDLK_x:
            cpu_set_joypad_button(cpu, JoypadButton::B, pressed);
            break;
        case SDLK_RSHIFT:
            cpu_set_joypad_button(cpu, JoypadButton::Select, pressed);
            break;
        case SDLK_RETURN:
            cpu_set_joypad_button(cpu, JoypadButton::Start, pressed);
            break;
        default:
            break;
        }
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
    std::filesystem::path save_path;
    std::filesystem::path state_path;

    if (argc == 2)
    {
        const std::filesystem::path rom_path(argv[1]);
        state_path = rom_path;
        state_path.replace_extension(".state");
        std::string rom_error;
        if (!load_rom(cpu, rom_path, rom_error))
        {
            std::cerr << "Unable to load ROM: " << rom_path << " (" << rom_error << ")\n\n";
            print_usage(argv[0]);
            return 1;
        }

        if (cpu.cartridge_battery_backed)
        {
            save_path = rom_path;
            save_path.replace_extension(".sav");
            if (!load_cartridge_ram(cpu, save_path, rom_error))
            {
                std::cerr << "Unable to load cartridge save: " << rom_error << std::endl;
                return 1;
            }
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

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
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
    cpu.ppu = &ppu;

    SDL_AudioSpec audio_request{};
    audio_request.freq = APU_LR35902::SAMPLE_RATE;
    audio_request.format = AUDIO_S16SYS;
    audio_request.channels = 2;
    audio_request.samples = 1024;
    SDL_AudioSpec audio_spec{};
    const SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(nullptr, 0, &audio_request, &audio_spec, 0);
    if (audio_device != 0)
    {
        cpu.apu.sample_output = true;
        SDL_PauseAudioDevice(audio_device, 0);
    }
    else
    {
        std::cerr << "Audio unavailable: " << SDL_GetError() << std::endl;
    }
    // The audio device's clock drifts slightly from the emulator's, so the
    // audio output rate is nudged by up to one percent to hold the queue near
    // its target. Audio is only dropped if the queue runs far ahead.
    constexpr Uint32 AUDIO_FRAME_BYTES = 2 * sizeof(int16_t);
    constexpr double TARGET_QUEUED_AUDIO_FRAMES = APU_LR35902::SAMPLE_RATE * 0.05;
    constexpr Uint32 MAX_QUEUED_AUDIO_BYTES = APU_LR35902::SAMPLE_RATE / 4 * AUDIO_FRAME_BYTES;
    constexpr double MAX_AUDIO_RATE_ADJUSTMENT = 0.01;
    const double nominal_cycles_per_sample = cpu.apu.cycles_per_sample;

    // Frames are paced to the Game Boy's 59.73 Hz by the clock, whatever the
    // display's refresh rate (which SDL does not always report accurately).
    // With vsync, the latest frame is shown on each refresh.
    const Uint64 performance_frequency = SDL_GetPerformanceFrequency();
    const Uint64 ticks_per_frame = performance_frequency * CYCLES_PER_FRAME / 4194304;
    Uint64 next_frame_time = SDL_GetPerformanceCounter();

    bool running = true;
    bool fast_forward = false;
    int cycle_accumulator = 0;

    // Audio is dropped while fast-forwarding.
    const auto run_frame = [&](bool play_audio)
    {
        while (cycle_accumulator < CYCLES_PER_FRAME)
        {
            cycle_accumulator += step_cpu(cpu);
        }
        cycle_accumulator -= CYCLES_PER_FRAME;

        if (!play_audio)
        {
            cpu.apu.samples.clear();
        }
        else if (audio_device != 0)
        {
            const Uint32 queued_bytes = SDL_GetQueuedAudioSize(audio_device);
            if (queued_bytes < MAX_QUEUED_AUDIO_BYTES)
            {
                SDL_QueueAudio(audio_device, cpu.apu.samples.data(),
                               static_cast<Uint32>(cpu.apu.samples.size() * sizeof(int16_t)));
            }
            cpu.apu.samples.clear();

            // A fuller queue means fewer samples per emulated second, and vice versa.
            const double queued_frames = static_cast<double>(SDL_GetQueuedAudioSize(audio_device)) / AUDIO_FRAME_BYTES;
            const double fill_error = std::clamp(
                (queued_frames - TARGET_QUEUED_AUDIO_FRAMES) / TARGET_QUEUED_AUDIO_FRAMES, -1.0, 1.0);
            cpu.apu.cycles_per_sample = nominal_cycles_per_sample * (1.0 + MAX_AUDIO_RATE_ADJUSTMENT * fill_error);
        }
    };

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
            {
                const bool pressed = event.type == SDL_KEYDOWN;
                const SDL_Keycode key = event.key.keysym.sym;
                if (pressed && key == SDLK_ESCAPE)
                {
                    running = false;
                }
                else if (key == SDLK_TAB)
                {
                    fast_forward = pressed;
                }
                else if (pressed && event.key.repeat == 0 && key == SDLK_F5 && !state_path.empty())
                {
                    std::string state_error;
                    if (save_state_file(cpu, ppu, state_path, state_error))
                    {
                        std::cout << "Saved state to " << state_path.string() << std::endl;
                    }
                    else
                    {
                        std::cerr << "Unable to save state: " << state_error << std::endl;
                    }
                }
                else if (pressed && event.key.repeat == 0 && key == SDLK_F8 && !state_path.empty())
                {
                    std::string state_error;
                    if (load_state_file(cpu, ppu, state_path, state_error))
                    {
                        std::cout << "Loaded state from " << state_path.string() << std::endl;
                        if (audio_device != 0)
                        {
                            SDL_ClearQueuedAudio(audio_device);
                        }
                    }
                    else
                    {
                        std::cerr << "Unable to load state: " << state_error << std::endl;
                    }
                }
                else
                {
                    update_joypad_from_key(cpu, key, pressed);
                }
            }
        }
        if (!running)
            break;

        const Uint64 now = SDL_GetPerformanceCounter();
        if (fast_forward)
        {
            for (int frame = 0; frame < FAST_FORWARD_FRAMES; ++frame)
            {
                run_frame(false);
            }
            next_frame_time = now + ticks_per_frame;
        }
        else
        {
            if (now > next_frame_time + 4 * ticks_per_frame)
            {
                next_frame_time = now; // Fell far behind: don't try to catch up in a burst.
            }
            while (next_frame_time <= now)
            {
                run_frame(true);
                next_frame_time += ticks_per_frame;
            }
        }

        SDL_UpdateTexture(texture, nullptr, ppu.framebuffer.data(), GB_WIDTH * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        if (!renderer_vsync)
        {
            const Uint64 now = SDL_GetPerformanceCounter();
            if (next_frame_time > now)
            {
                SDL_Delay(static_cast<Uint32>((next_frame_time - now) * 1000 / performance_frequency));
            }
        }
    }

    if (audio_device != 0)
    {
        SDL_CloseAudioDevice(audio_device);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!save_path.empty())
    {
        std::string save_error;
        if (!save_cartridge_ram(cpu, save_path, save_error))
        {
            std::cerr << "Unable to save cartridge RAM: " << save_error << std::endl;
            return 1;
        }
    }

    std::cout << "Final CPU state:\n";
    print_cpu_state(cpu);
    return 0;
}
