#include "LR35902.h"
#include "PPU.h"

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

    int ppu_write_cycle(CPU_LR35902 &cpu, std::uint8_t opcode)
    {
        std::uint16_t address = 0;
        int access_cycle = 0;
        switch (opcode)
        {
        case 0xE0:
            address = static_cast<std::uint16_t>(0xFF00u + cpu_read(cpu, static_cast<std::uint16_t>(cpu.pc + 1)));
            access_cycle = 8;
            break;
        case 0xEA:
            address = cpu_read16(cpu, static_cast<std::uint16_t>(cpu.pc + 1));
            access_cycle = 16;
            break;
        case 0xE2:
            address = static_cast<std::uint16_t>(0xFF00u + cpu.bc.reg8.low);
            access_cycle = 8;
            break;
        case 0x77:
            address = cpu.hl.reg16;
            access_cycle = 8;
            break;
        case 0x36:
            address = cpu.hl.reg16;
            access_cycle = 12;
            break;
        case 0xCB:
        {
            const uint8_t cb_opcode = cpu_read(cpu, static_cast<uint16_t>(cpu.pc + 1));
            const uint8_t operation = cb_opcode >> 6;
            if ((cb_opcode & 0x07) == 6 && operation >= 2)
            {
                address = cpu.hl.reg16;
                access_cycle = 16;
            }
            break;
        }
        default:
            return 0;
        }

        return address >= 0xFF40 && address <= 0xFF4B ? access_cycle : 0;
    }

    bool has_mooneye_pass_signature(const CPU_LR35902 &cpu)
    {
        return cpu.bc.reg8.high == 3 && cpu.bc.reg8.low == 5 &&
               cpu.de.reg8.high == 8 && cpu.de.reg8.low == 13 &&
               cpu.hl.reg8.high == 21 && cpu.hl.reg8.low == 34;
    }

    bool has_mooneye_fail_signature(const CPU_LR35902 &cpu)
    {
        return cpu.bc.reg8.high == 0x42 && cpu.bc.reg8.low == 0x42 &&
               cpu.de.reg8.high == 0x42 && cpu.de.reg8.low == 0x42 &&
               cpu.hl.reg8.high == 0x42 && cpu.hl.reg8.low == 0x42;
    }

    void print_failure_diagnostics(const CPU_LR35902 &cpu, std::uint64_t cycles_run)
    {
        std::cerr << "\nROM test failed after " << std::dec << cycles_run << " cycles"
                  << " at PC=0x" << std::hex << cpu.pc
                  << " AF=0x" << cpu.af.reg16
                  << " BC=0x" << cpu.bc.reg16
                  << " DE=0x" << cpu.de.reg16
                  << " HL=0x" << cpu.hl.reg16
                  << " SP=0x" << cpu.sp
                  << " DIV/TIMA/TMA/TAC=" << static_cast<unsigned>(cpu.memory[0xFF04]) << '/'
                  << static_cast<unsigned>(cpu.memory[0xFF05]) << '/'
                  << static_cast<unsigned>(cpu.memory[0xFF06]) << '/'
                  << static_cast<unsigned>(cpu.memory[0xFF07])
                  << " IF/IE=" << static_cast<unsigned>(cpu.memory[0xFF0F]) << '/'
                  << static_cast<unsigned>(cpu.memory[0xFFFF])
                  << " LCDC/STAT/LY/LYC=" << static_cast<unsigned>(cpu.memory[0xFF40]) << '/'
                  << static_cast<unsigned>(cpu.memory[0xFF41]) << '/'
                  << static_cast<unsigned>(cpu.memory[0xFF44]) << '/'
                  << static_cast<unsigned>(cpu.memory[0xFF45])
                  << " locks=" << cpu.vram_locked << '/' << cpu.oam_locked
                  << "\nHRAM[FF80..FF90]:";
        for (std::uint16_t address = 0xFF80; address <= 0xFF90; ++address)
        {
            std::cerr << ' ' << static_cast<unsigned>(cpu.memory[address]);
        }
        std::cerr << std::dec << '\n';
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
    PPU_LR35902 ppu{};
    std::size_t output_written = 0;
    while (cycles_run < cycle_limit && !cpu.stopped)
    {
        const uint16_t pc_before = cpu.pc;
        const uint8_t opcode = cpu_read(cpu, pc_before);
        const int ppu_access_cycle = ppu_write_cycle(cpu, opcode);
        if (ppu_access_cycle != 0)
        {
            update_ppu(cpu, ppu, ppu_access_cycle);
        }
        const int cycles = step_cpu(cpu);
        if (cycles <= 0)
        {
            break;
        }
        cycles_run += static_cast<std::uint64_t>(cycles);
        update_ppu(cpu, ppu, cycles - ppu_access_cycle);

        if (cpu.serial_output.size() > output_written)
        {
            std::cout.write(cpu.serial_output.data() + output_written,
                            static_cast<std::streamsize>(cpu.serial_output.size() - output_written));
            std::cout.flush();
            output_written = cpu.serial_output.size();
        }

        const bool executed_ld_b_b = opcode == 0x40 &&
                                     cpu.pc == static_cast<uint16_t>(pc_before + 1);
        if (cpu.serial_output.find("Passed") != std::string::npos)
        {
            std::cout << "\nROM test passed after " << cycles_run << " cycles.\n";
            return 0;
        }
        if (cpu.serial_output.find("Failed") != std::string::npos)
        {
            print_failure_diagnostics(cpu, cycles_run);
            return 1;
        }

        if (executed_ld_b_b && has_mooneye_pass_signature(cpu))
        {
            std::cout << "\nMooneye ROM test passed after " << cycles_run << " cycles.\n";
            return 0;
        }

        if (executed_ld_b_b && has_mooneye_fail_signature(cpu))
        {
            print_failure_diagnostics(cpu, cycles_run);
            return 1;
        }
    }

    std::cerr << "\nNo Passed/Failed serial marker after " << cycles_run << " cycles.\n"
              << "PC=0x" << std::hex << cpu.pc
              << " SB=0x" << static_cast<unsigned>(cpu_read(cpu, 0xFF01))
              << " SC=0x" << static_cast<unsigned>(cpu_read(cpu, 0xFF02))
              << " serial-bytes=" << std::dec << cpu.serial_output.size()
              << " transfer-active=" << cpu.serial_transfer_active
              << " OAM-DMA=" << cpu.oam_dma_active << ':' << cpu.oam_dma_index
              << ':' << static_cast<unsigned>(cpu.oam_dma_startup_delay)
              << " AF/BC/DE/HL/SP=" << cpu.af.reg16 << '/' << cpu.bc.reg16 << '/'
              << cpu.de.reg16 << '/' << cpu.hl.reg16 << '/' << cpu.sp << '\n';
    return 2;
}
