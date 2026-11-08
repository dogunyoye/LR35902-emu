#include "SaveState.h"

#include <cassert>

namespace
{
    // A ROM-only cartridge that keeps changing WRAM, the background palette,
    // and a sound register while the PPU runs.
    std::vector<uint8_t> make_rom()
    {
        std::vector<uint8_t> rom(0x8000, 0);
        const uint8_t program[] = {
            0x3C,             // INC A
            0xEA, 0x00, 0xC0, // LD ($C000), A
            0xE0, 0x47,       // LDH ($47), A
            0xE0, 0x13,       // LDH ($13), A
            0xC3, 0x00, 0x01, // JP $0100
        };
        std::copy(std::begin(program), std::end(program), rom.begin() + 0x0100);
        return rom;
    }

    void run_steps(CPU_LR35902 &cpu, int steps)
    {
        for (int step = 0; step < steps; ++step)
        {
            step_cpu(cpu);
        }
    }
}

static void test_round_trip_resumes_identically()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    std::string error;
    assert(load_cartridge(cpu, make_rom(), error));
    cpu.ppu = &ppu; // Loading a cartridge resets the CPU.
    run_steps(cpu, 50000);

    const std::vector<uint8_t> state = serialize_state(cpu, ppu);
    run_steps(cpu, 30000);
    const std::vector<uint8_t> expected = serialize_state(cpu, ppu);
    assert(expected != state);

    assert(deserialize_state(cpu, ppu, state, error));
    assert(serialize_state(cpu, ppu) == state);
    assert(cpu.ppu == &ppu);
    run_steps(cpu, 30000);
    assert(serialize_state(cpu, ppu) == expected);
}

static void test_rejected_states_leave_machine_unchanged()
{
    CPU_LR35902 cpu{};
    PPU_LR35902 ppu{};
    std::string error;
    assert(load_cartridge(cpu, make_rom(), error));
    cpu.ppu = &ppu; // Loading a cartridge resets the CPU.
    run_steps(cpu, 1000);
    const std::vector<uint8_t> state = serialize_state(cpu, ppu);
    run_steps(cpu, 1000);
    const std::vector<uint8_t> before = serialize_state(cpu, ppu);

    std::vector<uint8_t> truncated(state.begin(), state.end() - 1);
    assert(!deserialize_state(cpu, ppu, truncated, error));
    assert(error == "the save state is damaged.");
    assert(serialize_state(cpu, ppu) == before);

    std::vector<uint8_t> bad_magic = state;
    bad_magic[0] = 'X';
    assert(!deserialize_state(cpu, ppu, bad_magic, error));
    assert(serialize_state(cpu, ppu) == before);

    CPU_LR35902 other{};
    PPU_LR35902 other_ppu{};
    std::vector<uint8_t> other_rom = make_rom();
    other_rom[0x0200] = 0xFF;
    assert(load_cartridge(other, std::move(other_rom), error));
    const std::vector<uint8_t> other_before = serialize_state(other, other_ppu);
    assert(!deserialize_state(other, other_ppu, state, error));
    assert(error == "the save state is for a different ROM.");
    assert(serialize_state(other, other_ppu) == other_before);
}

int main()
{
    test_round_trip_resumes_identically();
    test_rejected_states_leave_machine_unchanged();
    return 0;
}
