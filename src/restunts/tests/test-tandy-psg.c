/* Hardware register and waveform checks independent of the rendering implementation. */
#include <assert.h>
#include <string.h>
#include "../../../third_party/mame-psg/mame_psg.h"

#define TEST_CLOCK_HZ 3579545U
#define TEST_SAMPLE_RATE 44100U
#define TEST_DIVIDED_CLOCK_HZ 1600U
#define TEST_DIVIDED_SAMPLE_RATE 100U
#define TEST_MAX_VOLUME 8191
#define TEST_NOISE_SEED 0x8000U
#define TEST_TONE_ZERO_PERIOD 1024U
#define TEST_NOISE_FIXED_PERIOD 32U
#define TEST_WAVE_FRAMES 2048U
#define TEST_CHUNK_FRAMES 37U
#define TEST_REFERENCE_PERIOD 13U
#define TEST_INITIAL_EDGE_CLOCKS 14U
#define TEST_CLOCK_DIVIDER 16U

static void initialize(struct MAME_PSG *chip, enum MAME_PSG_PROFILE profile)
{
	assert(mame_psg_initialize(chip, profile, TEST_DIVIDED_CLOCK_HZ, TEST_DIVIDED_SAMPLE_RATE));
}

static void mute_all(struct MAME_PSG *chip)
{
	mame_psg_write(chip, 0x9f);
	mame_psg_write(chip, 0xbf);
	mame_psg_write(chip, 0xdf);
	mame_psg_write(chip, 0xff);
}

static void test_initialization_and_silence(void)
{
	struct MAME_PSG chip;
	assert(!mame_psg_initialize(NULL, MAME_PSG_NCR8496, TEST_CLOCK_HZ, TEST_SAMPLE_RATE));
	assert(!mame_psg_initialize(&chip, MAME_PSG_NCR8496, 0, TEST_SAMPLE_RATE));
	assert(!mame_psg_initialize(&chip, MAME_PSG_NCR8496, TEST_CLOCK_HZ, 0));
	assert(
		!mame_psg_initialize(&chip, (enum MAME_PSG_PROFILE) - 1, TEST_CLOCK_HZ, TEST_SAMPLE_RATE));
	initialize(&chip, MAME_PSG_NCR8496);
	assert(chip.lfsr == TEST_NOISE_SEED);
	for (legacy_u32 channel = 0; channel < MAME_PSG_CHANNELS; channel++) {
		assert(chip.volume[channel] == TEST_MAX_VOLUME);
		assert(chip.output[channel] == 0);
		assert(chip.counter[channel] == 0);
	}
	for (legacy_u32 channel = 0; channel < MAME_PSG_TONE_CHANNELS; channel++) {
		assert(chip.period[channel] == TEST_TONE_ZERO_PERIOD);
	}
	mute_all(&chip);
	legacy_s16 samples[TEST_WAVE_FRAMES];
	mame_psg_render(&chip, samples, TEST_WAVE_FRAMES);
	for (size_t frame = 0; frame < TEST_WAVE_FRAMES; frame++) {
		assert(samples[frame] == 0);
	}
	/* Muting does not stop the chip's oscillators. */
	assert(chip.counter[0] != 0);
	mame_psg_reset(&chip);
	assert(chip.lfsr == TEST_NOISE_SEED);
	assert(chip.volume[0] == TEST_MAX_VOLUME);
	assert(chip.counter[0] == 0);
}

static void test_registers_and_counter_phase(void)
{
	struct MAME_PSG chip;
	initialize(&chip, MAME_PSG_NCR8496);
	mame_psg_write(&chip, 0xc0);
	mame_psg_write(&chip, 0x00);
	mame_psg_write(&chip, 0xe3);
	assert(chip.period[2] == TEST_TONE_ZERO_PERIOD);
	assert(chip.period[3] == 2U * TEST_TONE_ZERO_PERIOD);
	mame_psg_write(&chip, 0xc5);
	mame_psg_write(&chip, 0x01);
	assert(chip.registers[4] == 0x15);
	assert(chip.period[2] == 21);
	assert(chip.period[3] == 42);
	legacy_s16 discarded;
	mame_psg_render(&chip, &discarded, 1);
	legacy_s32 previous_counter = chip.counter[2];
	mame_psg_write(&chip, 0xcf);
	assert(chip.registers[4] == 0x1f);
	assert(chip.period[3] == 62);
	mame_psg_write(&chip, 0x02);
	assert(chip.registers[4] == 0x2f);
	assert(chip.period[3] == 94);
	assert(chip.counter[2] == previous_counter);
	mame_psg_write(&chip, 0xd1);
	assert(chip.volume[2] == 6506);
	mame_psg_write(&chip, 0x0f);
	assert(chip.volume[2] == 0);
	assert(chip.registers[5] == 15);
	assert(chip.registers[4] == 0x2f);
	assert(chip.counter[2] == previous_counter);
}

static void test_tone_areas_and_profile_polarity(void)
{
	/* Period 2 gives transitions at input clocks 14, 46, 78, ... . Each sample
	 * covers 16 clocks, so the crossing samples average fourteen clocks against two. */
	static const legacy_s16 expected[] = {-6143, 8191, 6143, -8191, -6143, 8191};
	struct MAME_PSG ncr, pssj;
	initialize(&ncr, MAME_PSG_NCR8496);
	initialize(&pssj, MAME_PSG_PSSJ3);
	mute_all(&ncr);
	mute_all(&pssj);
	mame_psg_write(&ncr, 0x82);
	mame_psg_write(&pssj, 0x82);
	mame_psg_write(&ncr, 0x90);
	mame_psg_write(&pssj, 0x90);
	legacy_s16 ncr_samples[sizeof(expected) / sizeof(expected[0])];
	legacy_s16 pssj_samples[sizeof(expected) / sizeof(expected[0])];
	mame_psg_render(&ncr, ncr_samples, sizeof(expected) / sizeof(expected[0]));
	mame_psg_render(&pssj, pssj_samples, sizeof(expected) / sizeof(expected[0]));
	for (size_t frame = 0; frame < sizeof(expected) / sizeof(expected[0]); frame++) {
		assert(pssj_samples[frame] == expected[frame]);
		assert(ncr_samples[frame] == -expected[frame]);
	}
	/* A sample spanning two complete period-1 cycles has exactly zero net area. */
	assert(mame_psg_initialize(&pssj, MAME_PSG_PSSJ3, 6400, TEST_DIVIDED_SAMPLE_RATE));
	mute_all(&pssj);
	mame_psg_write(&pssj, 0x81);
	mame_psg_write(&pssj, 0x90);
	mame_psg_render(&pssj, pssj_samples, sizeof(expected) / sizeof(expected[0]));
	for (size_t frame = 0; frame < sizeof(expected) / sizeof(expected[0]); frame++) {
		assert(pssj_samples[frame] == 0);
	}
}

static void test_noise_feedback_and_mode_resets(void)
{
	/* NCR XNOR feedback from bits 1 and 5, starting with bit 15 set. */
	static const legacy_u16 white_states[] = {0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00,
											  0xff00, 0xff80, 0xffc0, 0xffe0, 0x7ff0, 0x3ff8,
											  0x1ffc, 0x0ffe, 0x87ff, 0xc3ff};
	struct MAME_PSG chip;
	initialize(&chip, MAME_PSG_NCR8496);
	mute_all(&chip);
	mame_psg_write(&chip, 0xe4);
	assert(chip.period[3] == TEST_NOISE_FIXED_PERIOD);
	legacy_s16 discarded[TEST_NOISE_FIXED_PERIOD];
	mame_psg_render(&chip, discarded, 1);
	assert(chip.lfsr == white_states[0]);
	for (size_t step = 1; step < sizeof(white_states) / sizeof(white_states[0]); step++) {
		mame_psg_render(&chip, discarded, TEST_NOISE_FIXED_PERIOD);
		assert(chip.lfsr == white_states[step]);
	}
	legacy_u32 continued_noise = chip.lfsr;
	mame_psg_write(&chip, 0xe4);
	assert(chip.lfsr == continued_noise);
	mame_psg_write(&chip, 0xe5);
	assert(chip.lfsr == continued_noise);
	assert(chip.period[3] == 64);
	/* The continuation-byte path is accepted, but does not trigger NCR mode reset. */
	mame_psg_write(&chip, 0x01);
	assert(chip.lfsr == continued_noise);
	assert(chip.registers[6] == 1);
	mame_psg_write(&chip, 0xe5);
	assert(chip.lfsr == TEST_NOISE_SEED);
	mame_psg_write(&chip, 0xe1);
	assert(chip.lfsr == TEST_NOISE_SEED);
	mame_psg_reset(&chip);
	mame_psg_write(&chip, 0xe0);
	mame_psg_render(&chip, discarded, 1);
	assert(chip.lfsr == 0x4000);
	/* A rate-only write leaves the periodic sequence in flight. */
	mame_psg_write(&chip, 0xe2);
	assert(chip.lfsr == 0x4000);
}

/* Closed-form square-wave integral in input-clock units; no chip counters or
 * resampler state are used by this independent high-precision reference. */
static legacy_fext reference_integral(legacy_fext clocks)
{
	legacy_fext initial_edge = TEST_INITIAL_EDGE_CLOCKS;
	if (clocks <= initial_edge) {
		return -clocks * TEST_MAX_VOLUME;
	}
	legacy_fext half_period = TEST_REFERENCE_PERIOD * TEST_CLOCK_DIVIDER;
	legacy_fext elapsed = clocks - initial_edge;
	legacy_u64 complete_halves = (legacy_u64)(elapsed / half_period);
	legacy_fext remainder = elapsed - complete_halves * half_period;
	legacy_fext area = (complete_halves & 1U) ? half_period - remainder : remainder;
	return (area - initial_edge) * TEST_MAX_VOLUME;
}

static void test_fractional_clock_and_fragmentation(void)
{
	struct MAME_PSG whole, fragmented;
	assert(mame_psg_initialize(&whole, MAME_PSG_PSSJ3, TEST_CLOCK_HZ, TEST_SAMPLE_RATE));
	mute_all(&whole);
	mame_psg_write(&whole, 0x8d);
	mame_psg_write(&whole, 0x90);
	fragmented = whole;
	legacy_s16 complete[TEST_WAVE_FRAMES], chunks[TEST_WAVE_FRAMES];
	mame_psg_render(&whole, complete, TEST_WAVE_FRAMES);
	for (size_t offset = 0; offset < TEST_WAVE_FRAMES; offset += TEST_CHUNK_FRAMES) {
		size_t remaining = TEST_WAVE_FRAMES - offset;
		size_t count = remaining < TEST_CHUNK_FRAMES ? remaining : TEST_CHUNK_FRAMES;
		mame_psg_render(&fragmented, chunks + offset, count);
	}
	assert(memcmp(complete, chunks, sizeof(complete)) == 0);
	assert(memcmp(&whole, &fragmented, sizeof(whole)) == 0);
	legacy_fext duration = (legacy_fext)TEST_CLOCK_HZ / TEST_SAMPLE_RATE;
	for (size_t frame = 0; frame < TEST_WAVE_FRAMES; frame++) {
		legacy_fext from = frame * duration;
		legacy_fext to = (frame + 1U) * duration;
		legacy_s32 expected =
			(legacy_s32)((reference_integral(to) - reference_integral(from)) / duration);
		legacy_s32 difference = complete[frame] - expected;
		/* Only the analytic oracle's final integer rounding can differ by one. */
		assert(difference >= -1 && difference <= 1);
	}
}

legacy_int main(void)
{
	test_initialization_and_silence();
	test_registers_and_counter_phase();
	test_tone_areas_and_profile_polarity();
	test_noise_feedback_and_mode_resets();
	test_fractional_clock_and_fragmentation();
	return 0;
}
