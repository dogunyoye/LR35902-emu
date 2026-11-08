#pragma once

#include <array>
#include <cstdint>
#include <vector>

// DMG audio processing unit: two square channels (the first with a frequency
// sweep), a wave channel, and a noise channel, mixed to stereo samples.
struct APU_LR35902
{
    static constexpr int SAMPLE_RATE = 48000;

    struct Channel
    {
        bool enabled = false;
        bool enabled_before_trigger = false;
        bool dac_enabled = false;
        bool length_enabled = false;
        int length = 0;
        int timer = 0;
        uint16_t frequency = 0;
        // Square channels: duty pattern and position.
        uint8_t duty = 0;
        uint8_t duty_step = 0;
        // Volume envelope (square and noise channels).
        uint8_t volume = 0;
        int envelope_timer = 0;
        bool envelope_running = false;
        // Wave channel: position in wave RAM and the sample last read.
        uint8_t wave_position = 0;
        uint8_t wave_sample = 0;
        int wave_read_cycles = 0;
        // Noise channel shift register.
        uint16_t lfsr = 0;
    };

    std::array<Channel, 4> channels{};
    // The CGB clears length counters on power off and lets wave RAM be read
    // while channel 3 plays.
    bool cgb = false;
    // Raw register values for $FF10-$FF2F, and wave RAM at $FF30-$FF3F.
    std::array<uint8_t, 0x20> registers{};
    std::array<uint8_t, 0x10> wave_ram{};
    bool powered = false;
    // Frame sequencer step (0-7), advanced at 512 Hz from the DIV counter.
    uint8_t frame_step = 0;

    // Channel 1 frequency sweep.
    uint16_t sweep_shadow = 0;
    int sweep_timer = 0;
    bool sweep_enabled = false;
    bool sweep_negated = false;

    // Stereo output, produced only when sample_output is set: interleaved
    // left/right samples since the emulator last drained them, plus
    // downsampling and DC-filter state.
    bool sample_output = false;
    // T-cycles per output sample. The emulator adjusts this slightly to keep
    // its audio queue level when the display and Game Boy frame rates differ.
    double cycles_per_sample = 4194304.0 / SAMPLE_RATE;
    std::vector<int16_t> samples;
    double sample_accumulator_left = 0;
    double sample_accumulator_right = 0;
    int sample_cycles = 0;
    double sample_phase = 0;
    double highpass_left = 0;
    double highpass_right = 0;
};

// Register state the DMG and MGB boot ROMs leave behind (channel 1 still on
// after the boot chime). The SGB boot ROM leaves sound off.
void apu_power_on_after_boot(APU_LR35902 &apu, bool channel1_on);
uint8_t apu_read(const APU_LR35902 &apu, uint16_t address);
void apu_write(APU_LR35902 &apu, uint16_t address, uint8_t value);
// Advances the channel timers and sample output by T-cycles.
void apu_tick(APU_LR35902 &apu, int cycles);
// Produces output while the system clock is stopped by STOP: the channels hold
// their levels, which the output filter decays to silence.
void apu_tick_stopped(APU_LR35902 &apu, int cycles);
// Clocks the frame sequencer; called when bit 4 of DIV falls.
void apu_frame_sequencer_step(APU_LR35902 &apu);
