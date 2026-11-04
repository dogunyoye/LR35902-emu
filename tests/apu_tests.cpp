#include "APU.h"

#include <cassert>

static void test_register_read_masks()
{
    APU_LR35902 apu{};
    apu_power_on_after_boot(apu, true);
    assert(apu_read(apu, 0xFF26) == 0xF1); // Powered, channel 1 still on after the boot chime
    assert(apu_read(apu, 0xFF13) == 0xFF); // Frequency registers are write-only
    assert(apu_read(apu, 0xFF15) == 0xFF);

    apu_write(apu, 0xFF11, 0x00);
    assert(apu_read(apu, 0xFF11) == 0x3F); // Only the duty bits read back
    apu_write(apu, 0xFF1C, 0x00);
    assert(apu_read(apu, 0xFF1C) == 0x9F);
}

static void test_length_counter_disables_channel()
{
    APU_LR35902 apu{};
    apu_power_on_after_boot(apu, false);
    apu_write(apu, 0xFF17, 0xF0); // Channel 2 DAC on
    apu_write(apu, 0xFF16, 0x3E); // Length 2
    apu_write(apu, 0xFF19, 0xC0); // Trigger with length enabled
    assert((apu_read(apu, 0xFF26) & 0x02) != 0);

    for (int step = 0; step < 4; ++step) // Lengths are clocked on every other step
    {
        apu_frame_sequencer_step(apu);
    }
    assert((apu_read(apu, 0xFF26) & 0x02) == 0);
}

static void test_dac_off_disables_channel()
{
    APU_LR35902 apu{};
    apu_power_on_after_boot(apu, true);
    apu_write(apu, 0xFF12, 0x00);
    assert((apu_read(apu, 0xFF26) & 0x01) == 0);
    apu_write(apu, 0xFF14, 0x80); // Triggering cannot enable a channel with its DAC off
    assert((apu_read(apu, 0xFF26) & 0x01) == 0);
}

static void test_power_off_clears_registers()
{
    APU_LR35902 apu{};
    apu_power_on_after_boot(apu, true);
    apu_write(apu, 0xFF30, 0x12);
    apu_write(apu, 0xFF26, 0x00);
    assert(apu_read(apu, 0xFF26) == 0x70);
    assert(apu_read(apu, 0xFF24) == 0x00);
    assert(apu_read(apu, 0xFF10) == 0x80);
    apu_write(apu, 0xFF24, 0x77); // Ignored while powered off
    assert(apu_read(apu, 0xFF24) == 0x00);
    assert(apu_read(apu, 0xFF30) == 0x12); // Wave RAM is kept

    apu_write(apu, 0xFF26, 0x80);
    apu_write(apu, 0xFF24, 0x77);
    assert(apu_read(apu, 0xFF24) == 0x77);
}

static void test_sample_output()
{
    APU_LR35902 apu{};
    apu_power_on_after_boot(apu, false);
    apu.sample_output = true;
    apu_write(apu, 0xFF17, 0xF0);
    apu_write(apu, 0xFF18, 0x00);
    apu_write(apu, 0xFF19, 0x87); // Channel 2 at about 1 kHz
    for (int cycle = 0; cycle < 4194304 / 10; cycle += 4)
    {
        apu_tick(apu, 4);
    }
    assert(apu.samples.size() >= 2 * (APU_LR35902::SAMPLE_RATE / 10 - 1));
    bool nonzero = false;
    for (const int16_t sample : apu.samples)
    {
        nonzero = nonzero || sample != 0;
    }
    assert(nonzero);
}

static void test_sample_output_while_powered_off()
{
    APU_LR35902 apu{};
    apu_power_on_after_boot(apu, false);
    apu.sample_output = true;
    apu_write(apu, 0xFF26, 0x00);
    for (int cycle = 0; cycle < 4194304 / 10; cycle += 4)
    {
        apu_tick(apu, 4);
    }
    assert(apu.samples.size() >= 2 * (APU_LR35902::SAMPLE_RATE / 10 - 1));
    for (const int16_t sample : apu.samples)
    {
        assert(sample == 0);
    }
}

int main()
{
    test_register_read_masks();
    test_length_counter_disables_channel();
    test_dac_off_disables_channel();
    test_power_off_clears_registers();
    test_sample_output();
    test_sample_output_while_powered_off();
    return 0;
}
