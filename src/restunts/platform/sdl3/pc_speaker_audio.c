/* The sequencer and renderer stay on the main thread. SDL consumes PCM;
 * DOS hardware interrupts consume only a locked, pre-rendered command ring. */
#include "pc_speaker_audio.h"
#include "../../c/platform.h"
#include <string.h>

#define PC_AUDIO_ERROR_BYTES 256U
#define PC_AUDIO_SAMPLE_RATE 44100U
#define PC_AUDIO_TICK_FRAMES (PC_AUDIO_SAMPLE_RATE / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define PC_AUDIO_MAX_QUEUED_TICKS 10U

static struct PC_SPEAKER_DRIVER pc_driver;
static legacy_char pc_error[PC_AUDIO_ERROR_BYTES];
static legacy_u8 pc_ready;
extern legacy_s32 sdl3_batch_mode;

struct PC_SPEAKER_DRIVER *pc_speaker_audio_driver(void)
{
	return &pc_driver;
}

const legacy_char *pc_speaker_audio_error(void)
{
	return pc_error;
}

#ifdef __DJGPP__
#include <dpmi.h>
#include <go32.h>
#include <inlines/pc.h>

#define PC_PIT_COUNTER0_PORT 0x40U
#define PC_PIT_COUNTER2_PORT 0x42U
#define PC_PIT_CONTROL_PORT 0x43U
#define PC_GATE_PORT 0x61U
#define PC_GATE_BITS 3U
#define PC_PIT_TIMER_MODE 0x34U
#define PC_PIT_SQUARE_MODE 0xb6U
#define PC_PIT_PULSE_MODE 0xb0U
#define PC_PIT_TIMER_PERIOD 65535U
#define PC_PIT_BIOS_PERIOD 65536U
#define PC_PWM_TARGET_RATE 17000U
#define PC_PWM_FRAME_CLOCKS (PC_SPEAKER_PIT_HZ / PC_PWM_TARGET_RATE)
#define PC_PWM_RENDER_RATE (PC_SPEAKER_PIT_HZ / PC_PWM_FRAME_CLOCKS)
#define PC_PWM_MIN_PULSE 5U
#define PC_PWM_BYTE_MAX 255U
#define PC_IRQ_VECTOR 8U
#define PC_PIC_COMMAND_PORT 0x20U
#define PC_PIC_EOI 0x20U
#define PC_PIC_READ_PENDING 0x0aU
#define PC_TIMER_PENDING_BIT 1U
#define PC_RING_ENTRIES 8192U
#define PC_RING_MASK (PC_RING_ENTRIES - 1U)
#define PC_RING_MAX_QUEUED                                                                         \
	(PC_PWM_RENDER_RATE * PC_AUDIO_MAX_QUEUED_TICKS / DOS_TIMER_REALTIME_TICKS_PER_SECOND)
#define PC_IRQ_ATTRIBUTES __attribute__((noinline, noclone, no_reorder, no_stack_protector, used))

enum PC_OUTPUT_MODE { PC_OUTPUT_SILENT, PC_OUTPUT_TONE, PC_OUTPUT_PWM };

struct PC_HARDWARE_FRAME {
	legacy_u16 period;
	legacy_u8 pulse;
	legacy_u8 mode;
};

struct PC_FAR_HANDLER {
	legacy_u32 offset;
	legacy_u16 selector;
} __attribute__((packed));

/* No heap pointers, allocation, library calls or sequencer state in IRQ0. */
static struct {
	struct PC_HARDWARE_FRAME ring[PC_RING_ENTRIES];
	volatile legacy_u32 read;
	volatile legacy_u32 write;
	volatile legacy_u32 pcm_until;
	volatile legacy_u64 clocks;
	volatile legacy_u8 retire_requested;
	volatile legacy_u8 retired;
	legacy_u32 bios_phase;
	struct PC_FAR_HANDLER old_handler;
	legacy_u16 period;
	legacy_u8 mode;
} pc_irq;
static _go32_dpmi_seginfo pc_old_vector;
static _go32_dpmi_seginfo pc_new_vector;
static volatile legacy_u8 pc_irq_installed;
static legacy_u8 pc_wrapper_allocated;
static legacy_u8 pc_hardware_ready;
static legacy_u8 pc_irq_data_locked;
static legacy_u8 pc_irq_code_locked;
static legacy_u8 pc_saved_gate;
static legacy_u8 pc_clock_adjusted;
static legacy_u64 pc_clock_origin;
static legacy_s64 pc_clock_adjustment;
static legacy_u64 pc_clock_last;
static legacy_u32 pc_frame_fraction;

extern legacy_s64 __real_uclock(void);

static PC_IRQ_ATTRIBUTES void pc_hardware_irq(void)
{
	if (pc_irq.retired) {
		pc_irq.clocks += PC_PIT_TIMER_PERIOD;
		goto chain_bios;
	}
	pc_irq.clocks += PC_PWM_FRAME_CLOCKS;
	legacy_u32 read = pc_irq.read;
	if (read != pc_irq.write) {
		struct PC_HARDWARE_FRAME frame = pc_irq.ring[read & PC_RING_MASK];
		pc_irq.read = read + 1U;
		if (frame.mode == PC_OUTPUT_PWM) {
			if (pc_irq.mode != PC_OUTPUT_PWM) {
				/* Mode0 is software-triggered. It produces the same low pulse
				 * as PC15's gate-triggered mode1 and works in DOSBox too. */
				outportb(PC_PIT_CONTROL_PORT, PC_PIT_PULSE_MODE);
				outportb(PC_GATE_PORT, inportb(PC_GATE_PORT) | PC_GATE_BITS);
			}
			outportb(PC_PIT_COUNTER2_PORT, frame.pulse);
			outportb(PC_PIT_COUNTER2_PORT, 0);
		} else if (frame.mode == PC_OUTPUT_TONE) {
			if (pc_irq.mode != PC_OUTPUT_TONE || pc_irq.period != frame.period) {
				outportb(PC_PIT_CONTROL_PORT, PC_PIT_SQUARE_MODE);
				outportb(PC_PIT_COUNTER2_PORT, frame.period & LEGACY_U8_MAX);
				outportb(PC_PIT_COUNTER2_PORT, frame.period >> LEGACY_BYTE_BITS);
				outportb(PC_GATE_PORT, inportb(PC_GATE_PORT) | PC_GATE_BITS);
			}
		} else if (pc_irq.mode != PC_OUTPUT_SILENT) {
			outportb(PC_GATE_PORT, inportb(PC_GATE_PORT) & ~PC_GATE_BITS);
		}
		pc_irq.mode = frame.mode;
		pc_irq.period = frame.period;
	} else if (pc_irq.mode == PC_OUTPUT_PWM) {
		/* A loading pause must not leave an arbitrary PWM/DC level enabled. */
		outportb(PC_GATE_PORT, inportb(PC_GATE_PORT) & ~PC_GATE_BITS);
		pc_irq.mode = PC_OUTPUT_SILENT;
	}
	pc_irq.bios_phase += PC_PWM_FRAME_CLOCKS;
	if (pc_irq.bios_phase >= PC_PIT_BIOS_PERIOD) {
		pc_irq.bios_phase -= PC_PIT_BIOS_PERIOD;
		if (pc_irq.retire_requested && LEGACY_S32_FROM_BITS(pc_irq.pcm_until - pc_irq.read) <= 0) {
			/* Restore the normal timer at a BIOS tick boundary, preserving
			 * its phase while the main thread removes our vector next tick. */
			outportb(PC_PIT_CONTROL_PORT, PC_PIT_TIMER_MODE);
			outportb(PC_PIT_COUNTER0_PORT, PC_PIT_TIMER_PERIOD & LEGACY_U8_MAX);
			outportb(PC_PIT_COUNTER0_PORT, PC_PIT_TIMER_PERIOD >> LEGACY_BYTE_BITS);
			pc_irq.retired = 1;
		}
		/* Supply the interrupt frame expected by the original IRET handler.
		 * It retains BIOS/DJGPP timer service and acknowledges this IRQ. */
	chain_bios:
		__asm__ volatile("pushfl\n\tlcall *%0" : : "m"(pc_irq.old_handler) : "memory", "cc");
	} else {
		outportb(PC_PIC_COMMAND_PORT, PC_PIC_EOI);
	}
}

static PC_IRQ_ATTRIBUTES void pc_hardware_irq_end(void)
{
}

/* Link wrapping affects SDL's DOS clock and delay implementation together.
 * The normal uclock path remains unchanged until this backend is selected. */
legacy_s64 __wrap_uclock(void)
{
	if (!pc_irq_installed) {
		legacy_s64 clock = __real_uclock();
		return pc_clock_adjusted ? clock + pc_clock_adjustment : clock;
	}
	legacy_u64 clocks;
	legacy_u16 count;
	legacy_u8 pending;
	legacy_u32 interval;
	do {
		clocks = pc_irq.clocks;
		interval = pc_irq.retired ? PC_PIT_TIMER_PERIOD : PC_PWM_FRAME_CLOCKS;
		outportb(PC_PIT_CONTROL_PORT, 0);
		count = inportb(PC_PIT_COUNTER0_PORT);
		count |= (legacy_u16)inportb(PC_PIT_COUNTER0_PORT) << LEGACY_BYTE_BITS;
		outportb(PC_PIC_COMMAND_PORT, PC_PIC_READ_PENDING);
		pending = inportb(PC_PIC_COMMAND_PORT) & PC_TIMER_PENDING_BIT;
		/* Retry if IRQ0 advanced while latching. Polling must never disable
		 * interrupts for DPMI calls at a 17kHz audio interrupt cadence. */
	} while (clocks != pc_irq.clocks);
	if (pending) {
		clocks += interval;
	}
	if (count <= interval) {
		clocks += interval - count;
	}
	clocks += pc_clock_origin;
	if (clocks < pc_clock_last) {
		clocks = pc_clock_last;
	}
	pc_clock_last = clocks;
	return (legacy_s64)clocks;
}

static legacy_int pc_hardware_initialize(void)
{
	pc_saved_gate = inportb(PC_GATE_PORT) & PC_GATE_BITS;
	memset(&pc_irq, 0, sizeof(pc_irq));
	pc_new_vector.pm_selector = _go32_my_cs();
	pc_new_vector.pm_offset = (legacy_u32)pc_hardware_irq;
	if (!pc_irq_data_locked) {
		pc_irq_data_locked = _go32_dpmi_lock_data(&pc_irq, sizeof(pc_irq)) == 0;
	}
	if (!pc_irq_code_locked) {
		pc_irq_code_locked =
			_go32_dpmi_lock_code(pc_hardware_irq, (legacy_u32)pc_hardware_irq_end -
													  (legacy_u32)pc_hardware_irq) == 0;
	}
	if (!pc_irq_data_locked || !pc_irq_code_locked ||
		_go32_dpmi_allocate_iret_wrapper(&pc_new_vector) != 0) {
		strcpy(pc_error, "Cannot lock PC speaker interrupt buffers.");
		return 0;
	}
	pc_wrapper_allocated = 1;
	pc_hardware_ready = 1;
	outportb(PC_GATE_PORT, inportb(PC_GATE_PORT) & ~PC_GATE_BITS);
	pc_frame_fraction = 0;
	return 1;
}

static void pc_hardware_deactivate(void)
{
	if (!pc_irq_installed) {
		return;
	}
	legacy_u64 last = (legacy_u64)__wrap_uclock();
	legacy_int interrupts = __dpmi_get_and_disable_virtual_interrupt_state();
	if (!pc_irq.retired) {
		outportb(PC_PIT_CONTROL_PORT, PC_PIT_TIMER_MODE);
		outportb(PC_PIT_COUNTER0_PORT, PC_PIT_TIMER_PERIOD & LEGACY_U8_MAX);
		outportb(PC_PIT_COUNTER0_PORT, PC_PIT_TIMER_PERIOD >> LEGACY_BYTE_BITS);
	}
	_go32_dpmi_set_protected_mode_interrupt_vector(PC_IRQ_VECTOR, &pc_old_vector);
	pc_irq_installed = 0;
	pc_clock_adjustment = (legacy_s64)last - __real_uclock();
	pc_clock_adjusted = 1;
	if (interrupts) {
		__dpmi_get_and_enable_virtual_interrupt_state();
	}
}

static legacy_int pc_hardware_activate(void)
{
	pc_irq.retire_requested = 0;
	if (pc_irq_installed && pc_irq.retired) {
		pc_hardware_deactivate();
	}
	if (pc_irq_installed) {
		return 1;
	}
	/* Initialise DJGPP's timer first; its first uclock call programs channel0. */
	pc_clock_origin = (legacy_u64)__wrap_uclock();
	pc_clock_last = pc_clock_origin;
	pc_irq.clocks = 0;
	pc_irq.read = 0;
	pc_irq.write = 0;
	pc_irq.pcm_until = 0;
	pc_irq.retired = 0;
	if (_go32_dpmi_get_protected_mode_interrupt_vector(PC_IRQ_VECTOR, &pc_old_vector) != 0) {
		return 0;
	}
	pc_irq.old_handler.offset = pc_old_vector.pm_offset;
	pc_irq.old_handler.selector = pc_old_vector.pm_selector;
	legacy_int interrupts = __dpmi_get_and_disable_virtual_interrupt_state();
	outportb(PC_PIT_CONTROL_PORT, 0);
	legacy_u16 previous = inportb(PC_PIT_COUNTER0_PORT);
	previous |= (legacy_u16)inportb(PC_PIT_COUNTER0_PORT) << LEGACY_BYTE_BITS;
	pc_irq.bios_phase = PC_PIT_TIMER_PERIOD - previous;
	if (_go32_dpmi_set_protected_mode_interrupt_vector(PC_IRQ_VECTOR, &pc_new_vector) != 0) {
		if (interrupts) {
			__dpmi_get_and_enable_virtual_interrupt_state();
		}
		return 0;
	}
	outportb(PC_PIT_CONTROL_PORT, PC_PIT_TIMER_MODE);
	outportb(PC_PIT_COUNTER0_PORT, PC_PWM_FRAME_CLOCKS);
	outportb(PC_PIT_COUNTER0_PORT, 0);
	pc_irq_installed = 1;
	if (interrupts) {
		__dpmi_get_and_enable_virtual_interrupt_state();
	}
	return 1;
}

static void pc_hardware_shutdown(void)
{
	if (pc_hardware_ready) {
		pc_hardware_deactivate();
		outportb(PC_GATE_PORT, (inportb(PC_GATE_PORT) & ~PC_GATE_BITS) | pc_saved_gate);
		pc_hardware_ready = 0;
	}
	if (pc_wrapper_allocated) {
		_go32_dpmi_free_iret_wrapper(&pc_new_vector);
		pc_wrapper_allocated = 0;
	}
}

static void pc_hardware_clear(void)
{
	legacy_int interrupts = __dpmi_get_and_disable_virtual_interrupt_state();
	pc_irq.read = pc_irq.write;
	pc_irq.pcm_until = pc_irq.read;
	pc_irq.mode = PC_OUTPUT_SILENT;
	outportb(PC_GATE_PORT, inportb(PC_GATE_PORT) & ~PC_GATE_BITS);
	if (interrupts) {
		__dpmi_get_and_enable_virtual_interrupt_state();
	}
}

static void pc_hardware_update(void)
{
	if (pc_irq_installed && pc_irq.retired) {
		pc_hardware_deactivate();
	}
	legacy_u32 queued = pc_irq.write - pc_irq.read;
	if (queued > PC_RING_MAX_QUEUED) {
		pc_hardware_clear();
	}
	pc_frame_fraction += PC_PWM_RENDER_RATE;
	legacy_u32 frames = pc_frame_fraction / DOS_TIMER_REALTIME_TICKS_PER_SECOND;
	pc_frame_fraction %= DOS_TIMER_REALTIME_TICKS_PER_SECOND;
	legacy_u32 pcm_frames = 0;
	if (pc_driver.selected_voice == 0 && pc_driver.sample_active) {
		legacy_u64 step =
			((legacy_u64)pc_driver.sample_rate << PC_SPEAKER_PHASE_BITS) / PC_PWM_RENDER_RATE;
		legacy_u64 end = (legacy_u64)pc_driver.sample_length << PC_SPEAKER_PHASE_BITS;
		if (step != 0 && end > pc_driver.sample_position) {
			legacy_u64 remaining = (end - pc_driver.sample_position + step - 1U) / step;
			pcm_frames = remaining < frames ? (legacy_u32)remaining : frames;
		}
	}
	legacy_s16 samples[(PC_PWM_RENDER_RATE + DOS_TIMER_REALTIME_TICKS_PER_SECOND - 1U) /
					   DOS_TIMER_REALTIME_TICKS_PER_SECOND];
	pc_speaker_driver_render(&pc_driver, samples, frames, PC_PWM_RENDER_RATE);
	if (pcm_frames != 0 && !pc_hardware_activate()) {
		return;
	}
	if (!pc_irq_installed) {
		/* Tones use channel2 directly; the OS timer and uclock remain normal. */
		if (pc_driver.output_active) {
			if (pc_irq.mode != PC_OUTPUT_TONE || pc_irq.period != pc_driver.output_period) {
				outportb(PC_PIT_CONTROL_PORT, PC_PIT_SQUARE_MODE);
				outportb(PC_PIT_COUNTER2_PORT, pc_driver.output_period & LEGACY_U8_MAX);
				outportb(PC_PIT_COUNTER2_PORT, pc_driver.output_period >> LEGACY_BYTE_BITS);
				outportb(PC_GATE_PORT, inportb(PC_GATE_PORT) | PC_GATE_BITS);
			}
			pc_irq.mode = PC_OUTPUT_TONE;
			pc_irq.period = pc_driver.output_period;
		} else {
			outportb(PC_GATE_PORT, inportb(PC_GATE_PORT) & ~PC_GATE_BITS);
			pc_irq.mode = PC_OUTPUT_SILENT;
		}
		return;
	}
	for (legacy_u32 index = 0; index < frames; index++) {
		struct PC_HARDWARE_FRAME frame = {0, 0, PC_OUTPUT_SILENT};
		if (index < pcm_frames) {
			legacy_u32 value =
				samples[index] / PC_SPEAKER_PCM_SAMPLE_SCALE + PC_SPEAKER_PCM_UNSIGNED_CENTER;
			frame.mode = PC_OUTPUT_PWM;
			frame.pulse = PC_PWM_MIN_PULSE +
						  (value * (PC_PWM_FRAME_CLOCKS - PC_PWM_MIN_PULSE)) / PC_PWM_BYTE_MAX;
		} else if (pc_driver.output_active) {
			frame.mode = PC_OUTPUT_TONE;
			frame.period = pc_driver.output_period;
		}
		legacy_u32 write = pc_irq.write;
		pc_irq.ring[write & PC_RING_MASK] = frame;
		if (frame.mode == PC_OUTPUT_PWM) {
			pc_irq.pcm_until = write + 1U;
		}
		/* The single IRQ reader cannot see this slot until it is complete. */
		__asm__ volatile("" : : : "memory");
		pc_irq.write = write + 1U;
	}
	if (pcm_frames == 0) {
		pc_irq.retire_requested = 1;
	}
}

#else
#include <SDL3/SDL.h>
static SDL_AudioStream *pc_stream;
static legacy_u8 pc_audio_subsystem;
#endif

void pc_speaker_audio_shutdown(void)
{
#ifdef __DJGPP__
	pc_hardware_shutdown();
#else
	SDL_DestroyAudioStream(pc_stream);
	pc_stream = NULL;
	if (pc_audio_subsystem) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		pc_audio_subsystem = 0;
	}
#endif
	pc_speaker_driver_shutdown(&pc_driver);
	pc_ready = 0;
}

legacy_int pc_speaker_audio_initialize(void)
{
	pc_speaker_audio_shutdown();
	pc_error[0] = 0;
	pc_speaker_driver_initialize(&pc_driver);
	if (sdl3_batch_mode) {
		return 1;
	}
#ifdef __DJGPP__
	if (!pc_hardware_initialize()) {
		pc_speaker_audio_shutdown();
		return 0;
	}
#else
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		SDL_snprintf(pc_error, sizeof(pc_error), "Cannot initialize PC speaker audio: %s",
					 SDL_GetError());
		pc_speaker_audio_shutdown();
		return 0;
	}
	pc_audio_subsystem = 1;
	SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, PC_AUDIO_SAMPLE_RATE};
	pc_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (pc_stream == NULL || !SDL_ResumeAudioStreamDevice(pc_stream)) {
		SDL_snprintf(pc_error, sizeof(pc_error), "Cannot open PC speaker audio: %s",
					 SDL_GetError());
		pc_speaker_audio_shutdown();
		return 0;
	}
#endif
	pc_ready = 1;
	return 1;
}

void pc_speaker_audio_set_music_muted(legacy_u8 muted)
{
	pc_speaker_driver_set_music_muted(&pc_driver, muted);
	if (!pc_ready) {
		return;
	}
#ifdef __DJGPP__
	pc_hardware_clear();
#else
	SDL_ClearAudioStream(pc_stream);
#endif
}

void pc_speaker_audio_update(void)
{
	if (!pc_ready) {
		if (sdl3_batch_mode) {
			legacy_s16 discarded[PC_AUDIO_TICK_FRAMES];
			pc_speaker_driver_render(&pc_driver, discarded, PC_AUDIO_TICK_FRAMES,
									 PC_AUDIO_SAMPLE_RATE);
		}
		return;
	}
#ifdef __DJGPP__
	pc_hardware_update();
#else
	const legacy_s32 max_queue =
		PC_AUDIO_TICK_FRAMES * sizeof(legacy_s16) * PC_AUDIO_MAX_QUEUED_TICKS;
	if (SDL_GetAudioStreamQueued(pc_stream) > max_queue) {
		SDL_ClearAudioStream(pc_stream);
	}
	legacy_s16 samples[PC_AUDIO_TICK_FRAMES];
	pc_speaker_driver_render(&pc_driver, samples, PC_AUDIO_TICK_FRAMES, PC_AUDIO_SAMPLE_RATE);
	SDL_PutAudioStreamData(pc_stream, samples, sizeof(samples));
#endif
}
