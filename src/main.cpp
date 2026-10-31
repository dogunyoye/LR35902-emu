#include "LR35902.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
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

    bool load_rom(CPU_LR35902 &cpu, const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return false;
        }

        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (rom.empty())
        {
            return false;
        }

        std::fill(cpu.memory.begin(), cpu.memory.begin() + static_cast<long long>(rom.size()), 0);
        std::copy(rom.begin(), rom.end(), cpu.memory.begin());
        cpu.pc = 0x0100;
        return true;
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
        if (!load_rom(cpu, rom_path))
        {
            std::cerr << "Unable to load ROM: " << rom_path << "\n\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    else
    {
        load_demo_program(cpu);
        std::cout << "LR35902 emulator\n";
        std::cout << "Executing a short demo program.\n\n";
    }

    if (argc == 2)
    {
        std::cout << "LR35902 emulator\n";
        std::cout << "Loading ROM: " << argv[1] << "\n\n";
    }

    for (int step = 0; step < 8; ++step)
    {
        std::cout << "Step " << step << ":\n";
        print_cpu_state(cpu);

        const int cycles = execute_instruction(cpu);
        update_timers(cpu, cycles);

        std::cout << "  cycles=" << cycles << "\n\n";

        if (cpu.halted)
        {
            break;
        }
    }

    std::cout << "Final CPU state:\n";
    print_cpu_state(cpu);
    return 0;
}
