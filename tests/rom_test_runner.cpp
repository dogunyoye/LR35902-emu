#include "LR35902.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    constexpr std::uint64_t DEFAULT_MAX_CYCLES = 50'000'000;

    void print_usage(const char *program_name)
    {
        std::cerr << "Usage: " << program_name << " ROM_PATH [MAX_CYCLES]\n"
                  << "Runs a serial-reporting ROM test. Exit status: 0=Passed, 1=Failed, 2=no result.\n";
    }

    bool parse_cycle_limit(const char *text, std::uint64_t &cycle_limit)
    {
        const std::string value(text);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), cycle_limit);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size() && cycle_limit > 0;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        print_usage(argv[0]);
        return 2;
    }

    std::uint64_t cycle_limit = DEFAULT_MAX_CYCLES;
    if (argc == 3 && !parse_cycle_limit(argv[2], cycle_limit))
    {
        std::cerr << "Invalid cycle limit.\n";
        print_usage(argv[0]);
        return 2;
    }

    const std::filesystem::path rom_path(argv[1]);
    std::ifstream file(rom_path, std::ios::binary);
    if (!file)
    {
        std::cerr << "Unable to open ROM: " << rom_path << '\n';
        return 2;
    }

    std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CPU_LR35902 cpu{};
    std::string error;
    if (!load_cartridge(cpu, std::move(rom), error))
    {
        std::cerr << "Unable to load ROM: " << error << '\n';
        return 2;
    }

    std::uint64_t cycles_run = 0;
    std::size_t output_written = 0;
    while (cycles_run < cycle_limit && !cpu.halted && !cpu.stopped)
    {
        const int cycles = step_cpu(cpu);
        if (cycles <= 0)
        {
            break;
        }
        cycles_run += static_cast<std::uint64_t>(cycles);

        if (cpu.serial_output.size() > output_written)
        {
            std::cout.write(cpu.serial_output.data() + output_written,
                            static_cast<std::streamsize>(cpu.serial_output.size() - output_written));
            std::cout.flush();
            output_written = cpu.serial_output.size();
        }

        if (cpu.serial_output.find("Passed") != std::string::npos)
        {
            std::cout << "\nROM test passed after " << cycles_run << " cycles.\n";
            return 0;
        }
        if (cpu.serial_output.find("Failed") != std::string::npos)
        {
            std::cerr << "\nROM test reported failure after " << cycles_run << " cycles.\n";
            return 1;
        }
    }

    std::cerr << "\nNo Passed/Failed serial marker after " << cycles_run << " cycles.\n";
    return 2;
}