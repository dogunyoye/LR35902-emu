#include "LR35902.h"
#include "PPU.h"
#include "SaveState.h"
#include "Controls.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr int GB_WIDTH = PPU_LR35902::WIDTH;
    constexpr int GB_HEIGHT = PPU_LR35902::HEIGHT;
    constexpr int DEFAULT_SCALE = 4;
    constexpr int CYCLES_PER_FRAME = 70224;
    // Frames run per displayed frame while fast-forwarding.
    constexpr int FAST_FORWARD_FRAMES = 4;

    void print_usage(const char *program_name)
    {
        std::cout << "Usage: " << program_name << " [OPTIONS] [ROM_PATH]\n"
                  << "\n"
                  << "Run the LR35902 emulator with either a Game Boy ROM or the built-in demo program.\n"
                  << "\n"
                  << "Options:\n"
                  << "  --scale N        Start with the window N times the Game Boy's size (1-10, default 4)\n"
                  << "  --fullscreen     Start in fullscreen\n"
                  << "  --model MODEL    auto (the default: a Game Boy Color for cartridges made for it,\n"
                  << "                   otherwise the original Game Boy), dmg, or cgb\n"
                  << "  --dmg-palette P  On a Game Boy Color, colour a game made for the original Game Boy\n"
                  << "                   with one of the boot ROM's manual choices instead of its own:\n"
                  << "                   right, left, up, down, right+a, left+a, up+a, down+a, right+b,\n"
                  << "                   left+b, up+b, or down+b (implies --model cgb)\n"
                  << "  --no-color-correction\n"
                  << "                   Show Game Boy Color colours unadjusted for its screen\n"
                  << "  --controls FILE  Read key bindings from FILE instead of controls.cfg in the\n"
                  << "                   emulator's settings folder\n"
                  << "  -h, --help       Show this help\n"
                  << "\n"
                  << "Examples:\n"
                  << "  " << program_name << "\n"
                  << "  " << program_name << " /path/to/game.gb\n"
                  << "  " << program_name << " --scale 3 /path/to/game.gb\n";
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

    enum class ModelChoice
    {
        Auto,
        Dmg,
        Cgb
    };

    bool load_rom(CPU_LR35902 &cpu, const std::filesystem::path &path, ModelChoice model, std::string &error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "could not open file.";
            return false;
        }

        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // By default, cartridges made for the Game Boy Color run on a CGB and
        // others on a DMG. Game Boy Color-only cartridges always need a CGB.
        const std::uint8_t cgb_flag = rom.size() > 0x143 ? rom[0x143] : 0;
        const bool cgb = (cgb_flag & 0xC0) == 0xC0 || model == ModelChoice::Cgb ||
                         (model == ModelChoice::Auto && (cgb_flag & 0x80) != 0);
        return load_cartridge(cpu, std::move(rom), error, cgb ? GameBoyModel::Cgb : GameBoyModel::DmgABC);
    }

    // Reads key bindings from the given controls file, or from controls.cfg in
    // the emulator's settings folder if none was given and one exists there.
    bool load_key_bindings(const std::filesystem::path &given_path, KeyBindings &bindings)
    {
        std::filesystem::path path = given_path;
        if (path.empty())
        {
            char *settings = SDL_GetPrefPath("", "lr35902-emu");
            if (settings == nullptr)
            {
                return true;
            }
            path = std::filesystem::path(settings) / "controls.cfg";
            SDL_free(settings);
            if (!std::filesystem::exists(path))
            {
                return true;
            }
        }

        std::ifstream file(path);
        if (!file)
        {
            std::cerr << "Unable to read controls file " << path.string() << std::endl;
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string error;
        if (!parse_key_bindings(text, bindings, error))
        {
            std::cerr << "Unable to use controls file " << path.string() << ": " << error << std::endl;
            return false;
        }
        std::cout << "Using controls from " << path.string() << "\n";
        return true;
    }

    // The Game Boy Color's screen shows its colours darker and less saturated
    // than a modern display does. This is the curve Gambatte uses to imitate
    // it, applied as frames are shown so screenshots keep the exact colours.
    std::vector<std::uint32_t> build_corrected_colors()
    {
        std::vector<std::uint32_t> colors(0x8000);
        for (std::uint32_t index = 0; index < colors.size(); ++index)
        {
            const std::uint32_t r = index & 0x1F;
            const std::uint32_t g = (index >> 5) & 0x1F;
            const std::uint32_t b = (index >> 10) & 0x1F;
            const auto scale = [](std::uint32_t value) { return value * 255 / 248; };
            colors[index] = 0xFF000000u | scale((r * 13 + g * 2 + b) >> 1) << 16 | scale((g * 3 + b) << 1) << 8 |
                            scale((r * 3 + g * 2 + b * 11) >> 1);
        }
        return colors;
    }

    // The RGB555 colour a CGB framebuffer pixel was widened from.
    std::size_t cgb_color_index(std::uint32_t pixel)
    {
        return ((pixel >> 19) & 0x1F) | ((pixel >> 11) & 0x1F) << 5 | ((pixel >> 3) & 0x1F) << 10;
    }

    std::optional<JoypadButton> joypad_button_for(Action action)
    {
        switch (action)
        {
        case Action::Up:
            return JoypadButton::Up;
        case Action::Down:
            return JoypadButton::Down;
        case Action::Left:
            return JoypadButton::Left;
        case Action::Right:
            return JoypadButton::Right;
        case Action::A:
            return JoypadButton::A;
        case Action::B:
            return JoypadButton::B;
        case Action::Select:
            return JoypadButton::Select;
        case Action::Start:
            return JoypadButton::Start;
        default:
            return std::nullopt;
        }
    }

    // Actions held on the keyboard, controller buttons, and controller sticks
    // and triggers. An action is held while any of them holds it.
    struct HeldActions
    {
        std::array<bool, ACTION_COUNT> keyboard{};
        std::array<bool, ACTION_COUNT> buttons{};
        std::array<bool, ACTION_COUNT> axes{};
        std::array<bool, ACTION_COUNT> applied{};

        bool held(Action action) const
        {
            const std::size_t index = static_cast<std::size_t>(action);
            return keyboard[index] || buttons[index] || axes[index];
        }
    };

    // Passes changes in the held Game Boy buttons to the emulated joypad, and
    // sets the tilt read by MBC7 cartridges.
    void apply_held_actions(CPU_LR35902 &cpu, HeldActions &held)
    {
        for (std::size_t index = 0; index < ACTION_COUNT; ++index)
        {
            const Action action = static_cast<Action>(index);
            const std::optional<JoypadButton> button = joypad_button_for(action);
            const bool now_held = held.held(action);
            if (button && now_held != held.applied[index])
            {
                cpu_set_joypad_button(cpu, *button, now_held);
            }
            held.applied[index] = now_held;
        }
        cpu.mbc7_tilt_x = (held.held(Action::TiltRight) ? 1 : 0) - (held.held(Action::TiltLeft) ? 1 : 0);
        cpu.mbc7_tilt_y = (held.held(Action::TiltDown) ? 1 : 0) - (held.held(Action::TiltUp) ? 1 : 0);
    }

    // Sticks act as directions once pushed halfway; the left stick moves and the
    // right stick tilts. The right trigger fast-forwards.
    void update_held_axis(HeldActions &held, SDL_GameControllerAxis axis, Sint16 value)
    {
        constexpr Sint16 THRESHOLD = 16384;
        const auto set = [&held](Action action, bool on) { held.axes[static_cast<std::size_t>(action)] = on; };
        switch (axis)
        {
        case SDL_CONTROLLER_AXIS_LEFTX:
            set(Action::Left, value < -THRESHOLD);
            set(Action::Right, value > THRESHOLD);
            break;
        case SDL_CONTROLLER_AXIS_LEFTY:
            set(Action::Up, value < -THRESHOLD);
            set(Action::Down, value > THRESHOLD);
            break;
        case SDL_CONTROLLER_AXIS_RIGHTX:
            set(Action::TiltLeft, value < -THRESHOLD);
            set(Action::TiltRight, value > THRESHOLD);
            break;
        case SDL_CONTROLLER_AXIS_RIGHTY:
            set(Action::TiltUp, value < -THRESHOLD);
            set(Action::TiltDown, value > THRESHOLD);
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            set(Action::FastForward, value > THRESHOLD);
            break;
        default:
            break;
        }
    }
}

int main(int argc, char **argv)
{
    std::filesystem::path rom_path;
    std::filesystem::path controls_path;
    int scale = DEFAULT_SCALE;
    ModelChoice model = ModelChoice::Auto;
    std::string dmg_palette;
    bool color_correction = true;
    bool start_fullscreen = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string arg = argv[index];
        if (arg == "-h" || arg == "--help")
        {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--fullscreen")
        {
            start_fullscreen = true;
        }
        else if (arg == "--no-color-correction")
        {
            color_correction = false;
        }
        else if ((arg == "--scale" || arg == "--controls" || arg == "--model" || arg == "--dmg-palette") &&
                 index + 1 < argc)
        {
            const std::string value = argv[++index];
            if (arg == "--controls")
            {
                controls_path = value;
                continue;
            }
            if (arg == "--model")
            {
                if (value != "auto" && value != "dmg" && value != "cgb")
                {
                    std::cerr << "The model must be auto, dmg, or cgb.\n\n";
                    print_usage(argv[0]);
                    return 1;
                }
                model = value == "dmg" ? ModelChoice::Dmg : value == "cgb" ? ModelChoice::Cgb : ModelChoice::Auto;
                continue;
            }
            if (arg == "--dmg-palette")
            {
                const std::vector<std::string> names = dmg_compatibility_palette_names();
                if (std::find(names.begin(), names.end(), value) == names.end())
                {
                    std::cerr << "Unknown DMG palette \"" << value << "\".\n\n";
                    print_usage(argv[0]);
                    return 1;
                }
                dmg_palette = value;
                continue;
            }
            try
            {
                scale = std::stoi(value);
            }
            catch (const std::exception &)
            {
                scale = 0;
            }
            if (scale < 1 || scale > 10)
            {
                std::cerr << "The scale must be a whole number from 1 to 10.\n\n";
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (arg.empty() || arg[0] == '-' || !rom_path.empty())
        {
            print_usage(argv[0]);
            return 1;
        }
        else
        {
            rom_path = arg;
        }
    }

    CPU_LR35902 cpu{};
    std::filesystem::path save_path;
    std::filesystem::path state_path;

    std::cout << "LR35902 emulator\n";
    if (!rom_path.empty())
    {
        state_path = rom_path;
        state_path.replace_extension(".state");
        std::string rom_error;
        if (!dmg_palette.empty() && model == ModelChoice::Dmg)
        {
            std::cerr << "--dmg-palette needs a Game Boy Color, not --model dmg.\n\n";
            print_usage(argv[0]);
            return 1;
        }
        if (!load_rom(cpu, rom_path, dmg_palette.empty() ? model : ModelChoice::Cgb, rom_error))
        {
            std::cerr << "Unable to load ROM: " << rom_path << " (" << rom_error << ")\n\n";
            print_usage(argv[0]);
            return 1;
        }
        if (!dmg_palette.empty() && !cpu.cgb_mode)
        {
            load_dmg_compatibility_palettes(cpu, dmg_palette);
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
        std::cout << "Loading ROM: " << rom_path.string() << "\n\n";
    }
    else
    {
        initialize_demo_vram(cpu);
        load_demo_program(cpu);
        std::cout << "Executing a short demo program.\n\n";
    }

    KeyBindings bindings = default_key_bindings();
    if (!load_key_bindings(controls_path, bindings))
    {
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    const std::string title = rom_path.empty() ? "LR35902 Emu" : "LR35902 Emu - " + rom_path.stem().string();
    SDL_Window *window =
        SDL_CreateWindow(title.c_str(),
                         SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED,
                         GB_WIDTH * scale,
                         GB_HEIGHT * scale,
                         SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                             (start_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
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
    // The picture keeps its shape and is scaled by whole numbers, with black
    // borders filling the rest of the window.
    SDL_RenderSetLogicalSize(renderer, GB_WIDTH, GB_HEIGHT);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);

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
    const std::vector<std::uint32_t> corrected_colors = build_corrected_colors();
    std::vector<std::uint32_t> corrected_frame(PPU_LR35902::PIXEL_COUNT);

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
    bool paused = false;
    int cycle_accumulator = 0;
    HeldActions held;
    std::vector<SDL_GameController *> controllers;

    // Audio is dropped while fast-forwarding.
    const auto run_frame = [&](bool play_audio)
    {
        while (cycle_accumulator < CYCLES_PER_FRAME)
        {
            const int cycles = step_cpu(cpu);
            cycle_accumulator += cpu.double_speed ? cycles / 2 : cycles; // Frames are counted in PPU dots.
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

    const auto set_paused = [&](bool pause)
    {
        paused = pause;
        SDL_SetWindowTitle(window, (paused ? title + " (paused)" : title).c_str());
        if (audio_device != 0)
        {
            SDL_ClearQueuedAudio(audio_device);
            SDL_PauseAudioDevice(audio_device, paused ? 1 : 0);
        }
    };

    // Actions that happen once when pressed rather than while held.
    const auto trigger = [&](Action action)
    {
        switch (action)
        {
        case Action::Quit:
            running = false;
            break;
        case Action::Pause:
            set_paused(!paused);
            break;
        case Action::ColorCorrection:
            color_correction = !color_correction;
            std::cout << "Colour correction " << (color_correction ? "on" : "off") << std::endl;
            break;
        case Action::Fullscreen:
        {
            const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            SDL_SetWindowFullscreen(window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        }
        case Action::SaveState:
        case Action::LoadState:
        {
            if (state_path.empty())
            {
                break;
            }
            std::string state_error;
            if (action == Action::SaveState)
            {
                if (save_state_file(cpu, ppu, state_path, state_error))
                {
                    std::cout << "Saved state to " << state_path.string() << std::endl;
                }
                else
                {
                    std::cerr << "Unable to save state: " << state_error << std::endl;
                }
            }
            else if (load_state_file(cpu, ppu, state_path, state_error))
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
            break;
        }
        default:
            break;
        }
    };

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                const std::optional<Action> action = action_for_key(bindings, event.key.keysym.sym);
                if (!action)
                {
                    break;
                }
                const bool pressed = event.type == SDL_KEYDOWN;
                held.keyboard[static_cast<std::size_t>(*action)] = pressed;
                if (pressed && event.key.repeat == 0)
                {
                    trigger(*action);
                }
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (SDL_GameController *controller = SDL_GameControllerOpen(event.cdevice.which))
                {
                    controllers.push_back(controller);
                    std::cout << "Controller connected: " << SDL_GameControllerName(controller) << std::endl;
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
            {
                SDL_GameController *controller = SDL_GameControllerFromInstanceID(event.cdevice.which);
                controllers.erase(std::remove(controllers.begin(), controllers.end(), controller), controllers.end());
                SDL_GameControllerClose(controller);
                held.buttons.fill(false); // Nothing stays held by a controller that has gone.
                held.axes.fill(false);
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
            {
                const std::optional<Action> action =
                    action_for_controller_button(static_cast<SDL_GameControllerButton>(event.cbutton.button));
                if (action)
                {
                    held.buttons[static_cast<std::size_t>(*action)] = event.type == SDL_CONTROLLERBUTTONDOWN;
                }
                break;
            }
            case SDL_CONTROLLERAXISMOTION:
                update_held_axis(held, static_cast<SDL_GameControllerAxis>(event.caxis.axis), event.caxis.value);
                break;
            default:
                break;
            }
        }
        if (!running)
            break;
        apply_held_actions(cpu, held);

        const Uint64 now = SDL_GetPerformanceCounter();
        if (paused)
        {
            next_frame_time = now + ticks_per_frame;
        }
        else if (held.held(Action::FastForward))
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

        if (cpu.cgb_hardware && color_correction)
        {
            for (std::size_t index = 0; index < corrected_frame.size(); ++index)
            {
                corrected_frame[index] = corrected_colors[cgb_color_index(ppu.framebuffer[index])];
            }
            SDL_UpdateTexture(texture, nullptr, corrected_frame.data(), GB_WIDTH * sizeof(uint32_t));
        }
        else
        {
            SDL_UpdateTexture(texture, nullptr, ppu.framebuffer.data(), GB_WIDTH * sizeof(uint32_t));
        }
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

    for (SDL_GameController *controller : controllers)
    {
        SDL_GameControllerClose(controller);
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
