#include <stddef.h>
#include "audio.h"
#include "audio_internal.h"
#include "externs.h"
#include "fileio.h"
#include "legacy.h"
#include "memmgr.h"
#include "platform.h"
#include "resource.h"
#include "timing.h"
#include "fatal.h"
#include "car_audio.h"
#include "audio_control.h"
#include "resource_bytes.h"
#ifdef RESTUNTS_SDL3
#include "../platform/sdl3/music.h"
#endif

#define AUDIO_DRIVER_TIMER_RATE 22U
#define AUDIO_DRIVER_DIRECT_CHANNEL_PERIOD 2
#define AUDIO_TIMER_INTERPOLATION_FRACTION_BITS 4U
#define AUDIO_TIMER_INTERPOLATION_CURRENT_WEIGHT 7U
#define AUDIO_TIMER_INTERPOLATION_AVERAGE_SHIFT 3U
#define DOS_SEGMENT_WRAP_PARAGRAPHS 4096U
#define AUDIO_ENGINE_FIRST_SAMPLE_RESOURCE 2U
#define AUDIO_ENGINE_RATE_DIVISOR_OFFSET 14U
#define AUDIO_ENGINE_RATE_BASE_OFFSET 15U
#define AUDIO_ENGINE_RATE_BASE_SHIFT 4U
#define AUDIO_ENGINE_CHANNEL_PRIORITY 127U
#define AUDIO_ENGINE_UNSET_VOLUME 255U
#define AUDIO_ENGINE_UNSET_PITCH 65535U
#define AUDIO_CHANNEL_DEFAULT_NOTE_VELOCITY 127U
#define AUDIO_CHANNEL_DEFAULT_NOTE_LIMIT 15U
#define AUDIO_CHANNEL_DEFAULT_RATE 127U
#define AUDIO_CHANNEL_NONE 255U
#define AUDIO_PRIORITY_LOWEST 255U
#define AUDIO_MAX_AUDIBLE_DISTANCE 6000
#define AUDIO_SPATIAL_INTERVAL_PERCENT 100U
#define AUDIO_APPROACH_VOLUME_REDUCTION_SHIFT 4U
#define AUDIO_VECTOR_X_OFFSET 0U
#define AUDIO_VECTOR_Y_OFFSET 2U
#define AUDIO_VECTOR_Z_OFFSET 4U
#define AUDIO_PERCUSSION_TYPE_OFFSET 5U
#define AUDIO_PERCUSSION_TYPE 5U
#define AUDIO_PERCUSSION_NOTE_FIRST 24U
#define AUDIO_PERCUSSION_NOTE_COUNT 16U
#define AUDIO_PERCUSSION_RESOURCE_COUNT 7U
#define AUDIO_RESOURCE_CONTEXT_MASK_OFFSET 12U
#define AUDIO_RESOURCE_BASE_PITCH_OFFSET 16U
#define AUDIO_RESOURCE_LEVEL_OFFSET 28U
#define AUDIO_RESOURCE_MODULATION_DELAY_OFFSET 42U
#define AUDIO_RESOURCE_MODULATION_COUNT_OFFSET 44U
#define AUDIO_RESOURCE_MODULATION_STEP_OFFSET 48U
#define AUDIO_RESOURCE_MODULATION_DIRECTION_OFFSET 52U
#define AUDIO_RESOURCE_SEQUENCE_DELAY_OFFSET 54U
#define AUDIO_RESOURCE_SEQUENCE_COUNT_OFFSET 56U
#define AUDIO_DIRECT_CHANNEL_DEFAULT_NOTE 60U
#define AUDIO_SAMPLE_CONTINUOUS_DURATION 4294967264UL
#define AUDIO_RESOURCE_ATTACK_LEVEL_OFFSET 30U
#define AUDIO_RESOURCE_ATTACK_STEP_OFFSET 32U
#define AUDIO_RESOURCE_DECAY_STEP_OFFSET 34U
#define AUDIO_RESOURCE_SUSTAIN_LEVEL_OFFSET 36U
#define AUDIO_RESOURCE_RELEASE_STEP_OFFSET 38U
#define AUDIO_RESOURCE_MODULATION_TICK_OFFSET 41U
#define AUDIO_RESOURCE_MODULATION_LIMIT_OFFSET 46U
#define AUDIO_RESOURCE_SEQUENCE_TICK_OFFSET 58U
#define AUDIO_RESOURCE_SEQUENCE_VALUES_OFFSET 59U
#define AUDIO_MODULATION_COUNT_INFINITE 32767U
#define AUDIO_MODULATION_REVERSE_AT_MINIMUM 1U
#define AUDIO_MODULATION_REVERSE_AT_MAXIMUM 2U
#define AUDIO_SEQUENCE_VALUE_INDEX_MASK 7U
#define AUDIO_DRIVER_MASTER_STATE_COMMAND 4
#define AUDIO_DEFAULT_MASTER_VOLUME 100U
#define AUDIO_EFFECT_RESOURCE_TYPE_OFFSET 5U
#define AUDIO_EFFECT_RESOURCE_ENTRY_COUNT_OFFSET 6U
#define AUDIO_EFFECT_RESOURCE_ENTRY_SIZE 4U
#define AUDIO_EFFECT_RESOURCE_TABLE_OFFSET 8U
#define AUDIO_EFFECT_RATE_SCALE 128UL
#define AUDIO_EFFECT_DEFAULT_PRIORITY 64U
#define AUDIO_EFFECT_HIGH_PRIORITY 100U
#define AUDIO_FADE_VOLUME_STEP 2
#define AUDIO_FADE_RESTORE_DELAY_TICKS 50UL
#define AUDIO_UNLOAD_FADE_DELAY_TICKS 2
#define AUDIO_SECONDARY_CHANNEL_VOLUME_REDUCTION 10U
#define AUDIO_CAR_EVENT_SCRAPE_FLAG 16U
#define AUDIO_CAR_EVENT_BUMP_FLAG 32U

enum AUDIO_MODULATION_DIRECTION {
	AUDIO_MODULATION_DIRECTION_INCREASE = 1,
	AUDIO_MODULATION_DIRECTION_DECREASE = 2
};

legacy_u8 audio_music_channel_count;
legacy_u8 audio_suspended;
legacy_u8 audio_music_active;
legacy_u8 audio_music_rate;
legacy_s8 audio_car_state_ready;
legacy_s8 audio_player_car_flags;
legacy_s8 audio_opponent_car_flags;
static legacy_u8 audio_saved_channel_volumes[AUDIO_CHANNEL_COUNT];
static legacy_u8 audio_effect_channel_volumes[AUDIO_CHANNEL_COUNT];
legacy_u8 audio_effect_rate;
legacy_u8 audio_channel_reserved[AUDIO_CHANNEL_COUNT];
static legacy_u8 audio_channel_values[AUDIO_CHANNEL_COUNT];
static legacy_u8 audio_channel_notes[AUDIO_CHANNEL_COUNT];
void far *audio_bass_drum_resource;
void far *audio_snare_resource;
void far *audio_tom_resource;
void far *audio_ride_resource;
void far *audio_crash_resource;
void far *audio_closed_hihat_resource;
void far *audio_open_hihat_resource;
legacy_s16 audio_player_engine_channel;
legacy_s16 audio_opponent_engine_channel;
static legacy_u16 audio_driver_timer_rate;
static legacy_s16 audio_driver_timer_divider;
legacy_u16 audio_sequence_elapsed_ticks;
legacy_u16 audio_sequence_tick_period;

void far *audio_read_far_pointer(const legacy_u8 far *source)
{
	return dos_memory_make_pointer(LEGACY_READ_U16_LE(source + 2), LEGACY_READ_U16_LE(source));
}

void audio_write_far_pointer(legacy_u8 far *destination, const void far *value)
{
	LEGACY_WRITE_U16_LE(destination, dos_memory_pointer_offset(value));
	LEGACY_WRITE_U16_LE(destination + 2, dos_memory_pointer_segment(value));
}

void audio_add_driver_timer(void)
{
	for (legacy_u16 index = 0; index < AUDIO_TIMER_COUNT; index++) {
		audio_timers[index].active = 0;
	}
	audio_driver_timer_rate = AUDIO_DRIVER_TIMER_RATE;
	timer_reg_callback(&audio_driver_timer);
}

void audio_remove_driver_timer(void)
{
	for (legacy_u16 index = 0; index < AUDIO_TIMER_COUNT; index++) {
		if (audio_timers[index].active == 1) {
			legacy_s16 channel = audio_timers[index].channel;
			audio_release_effect_channel(channel);
		}
		audio_timers[index].active = 0;
	}
	timer_remove_callback(&audio_driver_timer);
}

static void audio_copy_engine_definition(struct AUDIO_ENGINE_DEFINITION *engine_definition,
										 void far *source_pointer)
{
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	/* Full-game definitions contain native C pointers. Encode them into the
	 * sequencer's fixed four-byte pointer format instead of copying addresses. */
	const struct FULL_AUDIO_ENGINE_DEFINITION *source = source_pointer;
	engine_definition->sample_count = source->sample_count;
	for (legacy_u16 index = 0; index < AUDIO_ENGINE_RESERVED_PARAMETER_SIZE; ++index) {
		engine_definition->reserved_parameters[index] = source->reserved_parameters[index];
	}
	engine_definition->initialized = source->initialized;
	engine_definition->reserved_initialization_byte = source->reserved_initialization_byte;
	for (legacy_u16 index = 0; index < AUDIO_ENGINE_RESOURCE_COUNT; ++index) {
		audio_write_far_pointer((legacy_u8 *)&engine_definition->resources[index],
								source->resource_ids[index]);
	}
#else
	legacy_u16 source_offset = (legacy_u16)dos_memory_pointer_offset(source_pointer);
	legacy_u16 source_segment = (legacy_u16)dos_memory_pointer_segment(source_pointer);
	for (legacy_u16 field = 0; field < AUDIO_ENGINE_DEFINITION_SIZE; field++) {
		const legacy_u8 far *source =
			(const legacy_u8 far *)dos_memory_make_pointer(source_segment, source_offset);
		((legacy_u8 *)engine_definition)[field] = *source;
		source_offset++;
		if (source_offset == 0) {
			source_segment = LEGACY_U16_WRAP_ADD(source_segment, DOS_SEGMENT_WRAP_PARAGRAPHS);
		}
	}
#endif
}

static void audio_resolve_engine_definition(struct AUDIO_ENGINE_DEFINITION *engine_definition,
											void far *shape_resources, void far *audio_resources)
{
	if (engine_definition->initialized == 0) {
		void far *resource = locate_shape_fatal(
			(legacy_s8 far *)shape_resources, pad_id((const legacy_s8 far *)audio_read_far_pointer(
												  (legacy_u8 *)&engine_definition->resources[0])));
		audio_write_far_pointer((legacy_u8 *)&engine_definition->resources[0], resource);
		for (legacy_u16 resource_index = AUDIO_ENGINE_FIRST_SAMPLE_RESOURCE;
			 resource_index < AUDIO_ENGINE_RESOURCE_COUNT; resource_index++) {
			resource = init_audio_resources(
				audio_resources, shape_resources,
				pad_id((const legacy_s8 far *)audio_read_far_pointer(
					(legacy_u8 *)&engine_definition->resources[resource_index])));
			audio_write_far_pointer((legacy_u8 *)&engine_definition->resources[resource_index],
									resource);
		}
		engine_definition->initialized = 1;
	}
}

legacy_s16 audio_init_engine(legacy_s16 unused_type, void far *source_pointer,
							 void far *shape_resources, void far *audio_resources)
{
	(void)unused_type;
	legacy_u16 index;
	for (index = 0; index < AUDIO_TIMER_COUNT; index++) {
		if (audio_timers[index].active == 0) {
			break;
		}
	}
	if (index == AUDIO_TIMER_COUNT) {
		static const legacy_s8 exhausted_handles_message[] = "InitEngine: All handles used.";
		fatal_error(exhausted_handles_message);
		return -1;
	}

	struct AUDIO_TIMER *timer = &audio_timers[index];
	struct AUDIO_ENGINE_DEFINITION *engine_definition = &timer->definition;
	audio_copy_engine_definition(engine_definition, source_pointer);

	audio_resolve_engine_definition(engine_definition, shape_resources, audio_resources);

	legacy_s16 channel = audio_reserve_effect_channel(-1, AUDIO_ENGINE_CHANNEL_PRIORITY);
	timer->channel = channel;
	timer->engine_active = 0;
	timer->current_volume = 0;
	timer->current_pitch = 0;
	timer->target_volume = 0;
	const legacy_u8 far *definition = (const legacy_u8 far *)audio_read_far_pointer(
		(legacy_u8 *)&engine_definition->resources[0]);
	legacy_u16 divisor = definition[AUDIO_ENGINE_RATE_DIVISOR_OFFSET];
	legacy_u16 rate = LEGACY_U16_DIV_OR_ZERO(engine_definition->sample_count, divisor);
	rate =
		LEGACY_U16_WRAP_ADD(rate, (legacy_u16)((legacy_u16)definition[AUDIO_ENGINE_RATE_BASE_OFFSET]
											   << AUDIO_ENGINE_RATE_BASE_SHIFT));
	timer->target_pitch = rate;
	timer->last_volume = AUDIO_ENGINE_UNSET_VOLUME;
	timer->last_pitch = AUDIO_ENGINE_UNSET_PITCH;
	timer->engine_context = -1;
	timer->effect_channel = -1;
	timer->secondary_effect_channel = -1;
	timer->sample_count = engine_definition->sample_count;
	timer->parameters_changed = 0;
	timer->restart_engine = 0;
	timer->active = 1;
	return (legacy_s16)index;
}

void audio_start_engine(legacy_s16 index)
{
	struct AUDIO_TIMER *timer = &audio_timers[index];
	if (timer->active != 1 || timer->engine_active != 0) {
		return;
	}

	legacy_s16 handle = timer->channel;
	struct AUDIO_ENGINE_DEFINITION *engine_definition = &timer->definition;
	dos_audio_bind_channel_context(
		handle, audio_read_far_pointer((legacy_u8 *)&engine_definition->resources[0]));
	legacy_u16 sample_count = engine_definition->sample_count;
	const legacy_u8 far *definition = (const legacy_u8 far *)audio_read_far_pointer(
		(legacy_u8 *)&engine_definition->resources[0]);
	legacy_u16 divisor = definition[AUDIO_ENGINE_RATE_DIVISOR_OFFSET];
	legacy_u16 value = LEGACY_U16_DIV_OR_ZERO(sample_count, divisor);
	value = LEGACY_U16_WRAP_ADD(value,
								(legacy_u16)((legacy_u16)definition[AUDIO_ENGINE_RATE_BASE_OFFSET]
											 << AUDIO_ENGINE_RATE_BASE_SHIFT));
	timer->target_pitch = value;
	legacy_s16 channel = audio_start_sample(value, handle);
	timer->engine_context = channel;
	timer->engine_active = 1;
	timer->parameters_changed = 1;
	dos_audio_set_channel_volume(handle, 0);
}

void audio_update_engine_spatial(legacy_s16 index, legacy_s16 base_value, legacy_s16 first_x,
								 legacy_s16 first_y, legacy_s16 first_z, legacy_s16 second_x,
								 legacy_s16 second_y, legacy_s16 second_z, legacy_s16 interval)
{
	struct AUDIO_TIMER *timer = &audio_timers[index];
	legacy_u16 second_distance =
		(legacy_u16)polarRadius2D(polarRadius2D(second_x, second_z), second_y);
	if (LEGACY_S16_FROM_BITS(second_distance) > AUDIO_MAX_AUDIBLE_DISTANCE) {
		timer->target_volume = 0;
		return;
	}

	legacy_u16 first_distance = (legacy_u16)polarRadius2D(polarRadius2D(first_x, first_z), first_y);
	legacy_u16 distance_delta = LEGACY_U16_WRAP_SUB(first_distance, second_distance);
	legacy_u16 quotient =
		LEGACY_U16_DIV_OR_ZERO(AUDIO_SPATIAL_INTERVAL_PERCENT, (legacy_u16)interval);
	legacy_u16 scaled_delta = LEGACY_U16_WRAP_MUL(quotient, distance_delta);
	quotient = (legacy_u16)LEGACY_U32_DIV_OR_ZERO(
		LEGACY_U32_WRAP_MUL(AUDIO_ENGINE_MAX_VOLUME, second_distance), AUDIO_MAX_AUDIBLE_DISTANCE);
	legacy_u16 volume = LEGACY_U16_WRAP_SUB(AUDIO_ENGINE_MAX_VOLUME, quotient);
	if (LEGACY_S16_FROM_BITS(scaled_delta) > 0) {
		volume = LEGACY_U16_WRAP_SUB(volume, volume >> AUDIO_APPROACH_VOLUME_REDUCTION_SHIFT);
	}

	struct AUDIO_ENGINE_DEFINITION *engine_definition = &timer->definition;
	const legacy_u8 far *definition = (const legacy_u8 far *)audio_read_far_pointer(
		(legacy_u8 *)&engine_definition->resources[0]);
	legacy_u16 divisor = definition[AUDIO_ENGINE_RATE_DIVISOR_OFFSET];
	legacy_u16 base_rate = LEGACY_U16_DIV_OR_ZERO((legacy_u16)base_value, divisor);
	base_rate = LEGACY_U16_WRAP_ADD(
		base_rate, (legacy_u16)((legacy_u16)definition[AUDIO_ENGINE_RATE_BASE_OFFSET]
								<< AUDIO_ENGINE_RATE_BASE_SHIFT));
	legacy_u16 denominator = LEGACY_U16_WRAP_SUB(AUDIO_MAX_AUDIBLE_DISTANCE, scaled_delta);
	if (denominator != 0) {
		base_rate = (legacy_u16)LEGACY_U32_DIV_OR_ZERO(
			LEGACY_U32_WRAP_MUL(AUDIO_MAX_AUDIBLE_DISTANCE, base_rate), denominator);
		timer->target_pitch = base_rate;
	}
	timer->target_volume = (legacy_u8)volume;
}

void audio_apply_car_state_sample(const legacy_u8 far *sample, legacy_s16 interval)
{
	audio_update_engine_spatial(
		audio_player_engine_channel,
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(sample + AUDIO_CAR_STATE_PLAYER_RPM_OFFSET)),
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(sample + AUDIO_CAR_STATE_PLAYER_PREVIOUS_OFFSET +
												AUDIO_VECTOR_X_OFFSET)),
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(sample + AUDIO_CAR_STATE_PLAYER_PREVIOUS_OFFSET +
												AUDIO_VECTOR_Y_OFFSET)),
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(sample + AUDIO_CAR_STATE_PLAYER_PREVIOUS_OFFSET +
												AUDIO_VECTOR_Z_OFFSET)),
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(sample + AUDIO_CAR_STATE_PLAYER_CURRENT_OFFSET +
												AUDIO_VECTOR_X_OFFSET)),
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(sample + AUDIO_CAR_STATE_PLAYER_CURRENT_OFFSET +
												AUDIO_VECTOR_Y_OFFSET)),
		LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(sample + AUDIO_CAR_STATE_PLAYER_CURRENT_OFFSET +
												AUDIO_VECTOR_Z_OFFSET)),
		interval);
	if (gameconfig.game_opponenttype != 0) {
		audio_update_engine_spatial(
			audio_opponent_engine_channel,
			LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(sample + AUDIO_CAR_STATE_OPPONENT_RPM_OFFSET)),
			LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(
				sample + AUDIO_CAR_STATE_OPPONENT_PREVIOUS_OFFSET + AUDIO_VECTOR_X_OFFSET)),
			LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(
				sample + AUDIO_CAR_STATE_OPPONENT_PREVIOUS_OFFSET + AUDIO_VECTOR_Y_OFFSET)),
			LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(
				sample + AUDIO_CAR_STATE_OPPONENT_PREVIOUS_OFFSET + AUDIO_VECTOR_Z_OFFSET)),
			LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(
				sample + AUDIO_CAR_STATE_OPPONENT_CURRENT_OFFSET + AUDIO_VECTOR_X_OFFSET)),
			LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(
				sample + AUDIO_CAR_STATE_OPPONENT_CURRENT_OFFSET + AUDIO_VECTOR_Y_OFFSET)),
			LEGACY_S16_FROM_BITS(LEGACY_READ_U16_LE(
				sample + AUDIO_CAR_STATE_OPPONENT_CURRENT_OFFSET + AUDIO_VECTOR_Z_OFFSET)),
			interval);
	}
}

static void audio_update_timer_volume(struct AUDIO_TIMER *timer)
{
	legacy_u16 volume_accumulator = LEGACY_U16_WRAP_ADD(
		(legacy_u16)((legacy_u16)timer->target_volume << AUDIO_TIMER_INTERPOLATION_FRACTION_BITS),
		LEGACY_U16_WRAP_MUL(timer->current_volume, AUDIO_TIMER_INTERPOLATION_CURRENT_WEIGHT));
	volume_accumulator >>= AUDIO_TIMER_INTERPOLATION_AVERAGE_SHIFT;
	timer->current_volume = volume_accumulator;
	legacy_u8 volume = (legacy_u8)(volume_accumulator >> AUDIO_TIMER_INTERPOLATION_FRACTION_BITS);
	if (volume != timer->last_volume || timer->parameters_changed != 0) {
		legacy_s16 channel = timer->channel;
		dos_audio_set_channel_volume(channel, volume);
		legacy_u8 secondary_volume =
			volume >= AUDIO_SECONDARY_CHANNEL_VOLUME_REDUCTION
				? (legacy_u8)(volume - AUDIO_SECONDARY_CHANNEL_VOLUME_REDUCTION)
				: 0;
		channel = timer->effect_channel;
		if (channel != -1) {
			dos_audio_set_channel_volume(channel, secondary_volume);
		}
		channel = timer->secondary_effect_channel;
		if (channel != -1) {
			dos_audio_set_channel_volume(channel, secondary_volume);
		}
		timer->last_volume = volume;
	}
}

static void audio_update_timer_pitch(struct AUDIO_TIMER *timer)
{
	legacy_u32 accumulator = timer->current_pitch;
	accumulator = accumulator * AUDIO_TIMER_INTERPOLATION_CURRENT_WEIGHT +
				  ((legacy_u32)timer->target_pitch << AUDIO_TIMER_INTERPOLATION_FRACTION_BITS);
	accumulator >>= AUDIO_TIMER_INTERPOLATION_AVERAGE_SHIFT;
	timer->current_pitch = accumulator;
	legacy_u16 pitch = (legacy_u16)(accumulator >> AUDIO_TIMER_INTERPOLATION_FRACTION_BITS);
	if (pitch != timer->last_pitch || timer->parameters_changed != 0) {
		legacy_s16 channel = timer->engine_context;
		if (channel != -1) {
			dos_audio_set_context_pitch(channel, pitch);
			timer->last_pitch = pitch;
		}
	}
}

static void audio_restart_timer_engine(struct AUDIO_TIMER *timer, legacy_u16 index)
{
	if (timer->restart_engine != 0) {
		legacy_s16 channel = timer->effect_channel;
		if (timer->engine_active != 0) {
			audio_stop_effect_channel(channel);
			timer->restart_engine = 0;
		} else if (audio_effect_channel_idle(channel) != 0) {
			audio_start_engine(index);
			timer->restart_engine = 0;
		}
	}
}

void audio_driver_timer(void)
{
	if (dos_data_stack_segments_match() == 0) {
		return;
	}

	audio_driver_timer_divider = LEGACY_S16_WRAP_ADD(audio_driver_timer_divider, 1);
	if (audio_driver_timer_divider < AUDIO_DRIVER_DIRECT_CHANNEL_PERIOD &&
		dos_audio_uses_direct_channels != 0) {
		return;
	}

	struct AUDIO_TIMER *timer;
	for (legacy_u16 index = 0; index < AUDIO_TIMER_COUNT; index++) {
		timer = &audio_timers[index];
		if (timer->active == 0 || audio_effects_enabled == AUDIO_STATE_DISABLED) {
			continue;
		}

		audio_update_timer_volume(timer);

		audio_update_timer_pitch(timer);

		timer->parameters_changed = 0;
		audio_restart_timer_engine(timer, index);
	}

	if (audio_driver_timer_divider >= AUDIO_DRIVER_DIRECT_CHANNEL_PERIOD) {
		audio_driver_timer_divider = 0;
	}
}

void audio_unload(void)
{
	audio_fade_out(AUDIO_UNLOAD_FADE_DELAY_TICKS);
	mmgr_free(songfileptr);
	mmgr_free(voicefileptr);
	is_audioloaded = 0;
}

void audio_enable_music(void)
{
	audio_music_enabled = AUDIO_STATE_ENABLED;
#ifdef RESTUNTS_SDL3
	sdl3_music_sync();
#endif
}

void audio_disable_music(void)
{
	audio_music_enabled = AUDIO_STATE_DISABLED;
	audio_update_lock = AUDIO_UPDATE_LOCKED;
	if (audio_music_channel_count != 0) {
		audio_release_channel_range(0, (legacy_u16)audio_music_channel_count - 1U);
	}
	audio_update_driver_contexts();
	audio_update_lock = AUDIO_UPDATE_UNLOCKED;
#ifdef RESTUNTS_SDL3
	sdl3_music_sync();
#endif
}

legacy_s16 audio_toggle_music(void)
{
	if (audio_music_enabled == AUDIO_STATE_ENABLED) {
		audio_disable_music();
		return 0;
	}

	audio_enable_music();
	return 1;
}

legacy_s16 audio_effect_range_idle(void)
{
	if (audio_suspended == AUDIO_STATE_ENABLED || audio_music_enabled == AUDIO_STATE_DISABLED) {
		return 1;
	}

	for (legacy_u16 channel = 0; channel < (legacy_u16)audio_music_channel_count; channel++) {
		if ((audio_channels[channel + AUDIO_EFFECT_CHANNEL_FIRST].cursor.offset |
			 audio_channels[channel + AUDIO_EFFECT_CHANNEL_FIRST].cursor.segment) != 0) {
			return 0;
		}
	}

	return 1;
}

void audio_stop_music(void)
{
	audio_update_lock = AUDIO_UPDATE_LOCKED;
#ifdef RESTUNTS_SDL3
	sdl3_music_stop();
#endif
	audio_music_active = AUDIO_STATE_DISABLED;
	audio_release_channel_range(AUDIO_MUSIC_CHANNEL_FIRST, AUDIO_MUSIC_CHANNEL_LAST);
	audio_init_channel_range(AUDIO_MUSIC_CHANNEL_FIRST, AUDIO_MUSIC_CHANNEL_LAST, 0, 0,
							 audio_music_rate, 0);
	audio_music_channel_count = 0;
	audio_update_driver_contexts();
	audio_update_lock = AUDIO_UPDATE_UNLOCKED;
}

void audio_enable_effects(void)
{
	if (audio_effects_enabled == AUDIO_STATE_ENABLED) {
		return;
	}

	for (legacy_s16 channel = AUDIO_EFFECT_CHANNEL_FIRST;
		 (legacy_u16)channel <= AUDIO_EFFECT_CHANNEL_LAST; channel++) {
		dos_audio_set_channel_volume(channel, audio_effect_channel_volumes[channel]);
	}
	audio_effects_enabled = AUDIO_STATE_ENABLED;
}

void audio_disable_effects(void)
{
	if (audio_effects_enabled == AUDIO_STATE_DISABLED) {
		return;
	}

	for (legacy_s16 channel = AUDIO_EFFECT_CHANNEL_FIRST;
		 (legacy_u16)channel <= AUDIO_EFFECT_CHANNEL_LAST; channel++) {
		audio_effect_channel_volumes[channel] =
			audio_sfx_channels[channel - AUDIO_EFFECT_CHANNEL_FIRST].volume;
		dos_audio_set_channel_volume(channel, 0);
	}
	audio_effects_enabled = AUDIO_STATE_DISABLED;
}

legacy_s16 audio_toggle_effects(void)
{
	if (audio_effects_enabled == AUDIO_STATE_ENABLED) {
		audio_disable_effects();
		return 0;
	}

	audio_enable_effects();
	return 1;
}

legacy_s16 audio_effect_channel_idle(legacy_s16 channel)
{
	if (audio_effects_enabled == AUDIO_STATE_DISABLED ||
		(legacy_u16)channel < AUDIO_EFFECT_CHANNEL_FIRST ||
		(legacy_u16)channel > AUDIO_EFFECT_CHANNEL_LAST) {
		return 1;
	}

	return (audio_channels[channel].cursor.offset | audio_channels[channel].cursor.segment) == 0;
}

void audio_set_finish_callback(legacy_s16 channel, void far *value)
{
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
	audio_write_far_pointer((legacy_u8 *)&audio_channels[channel].finish_callback, value);
#else
	void far **field = (void far **)&audio_channels[channel].finish_callback;
	*field = value;
#endif
}

void audio_init_channel_range(legacy_s16 first_channel, legacy_s16 last_channel, void far *resource,
							  legacy_u16 resource_data_offset, legacy_u16 rate, legacy_u8 priority)
{
	legacy_s16 channel = (legacy_s16)first_channel;
	legacy_s16 last = (legacy_s16)last_channel;
	if (channel > last) {
		return;
	}

	legacy_u16 resource_offset = (legacy_u16)dos_memory_pointer_offset(resource);
	legacy_u16 resource_segment = (legacy_u16)dos_memory_pointer_segment(resource);
	struct AUDIO_CHANNEL *chunk;
	do {
		chunk = &audio_channels[channel];
		chunk->finish_callback.offset = 0;
		chunk->finish_callback.segment = 0;
		chunk->note_velocity = AUDIO_CHANNEL_DEFAULT_NOTE_VELOCITY;
		chunk->channel = (legacy_u8)channel;
		chunk->note_limit = AUDIO_CHANNEL_DEFAULT_NOTE_LIMIT;
		audio_channel_values[(legacy_u16)channel] = 0;
		audio_channel_notes[(legacy_u16)channel] = 0;
		chunk->stack_depth = 0;
		chunk->call_depth = 0;
		chunk->priority = priority;
		chunk->active_notes = 0;
		chunk->delay = 0;
		chunk->reserved_delay_byte = 0;
		chunk->resource.segment = 0;
		chunk->resource.offset = 0;
		chunk->volume = (legacy_u8)rate;
		chunk->sustain = 0;
		chunk->pitch = 0;
		chunk->driver_private_state[0] = 0;
		chunk->driver_private_state[1] = 0;
		chunk->driver_private_state[2] = 0;
		chunk->driver_private_state[3] = 0;
		chunk->driver_channel = AUDIO_CHANNEL_NONE;

		if (resource != 0) {
			const legacy_u8 far *resource_data = (const legacy_u8 far *)dos_memory_make_pointer(
				resource_segment, LEGACY_U16_WRAP_ADD(resource_offset, resource_data_offset));
			legacy_u32 pointer_value = resource_read_u32le(resource_data);
			legacy_u16 pointer_offset = LEGACY_U16_WRAP_ADD((legacy_u16)pointer_value, 4U);
			legacy_u16 pointer_segment = (legacy_u16)(pointer_value >> 16);
			LEGACY_WRITE_U16_LE((legacy_u8 *)&chunk->call_stack[0].offset, pointer_offset);
			LEGACY_WRITE_U16_LE((legacy_u8 *)&chunk->call_stack[0].segment, pointer_segment);
			pointer_value = resource_read_u32le(resource_data);
			pointer_offset = LEGACY_U16_WRAP_ADD((legacy_u16)pointer_value, 4U);
			pointer_segment = (legacy_u16)(pointer_value >> 16);
			LEGACY_WRITE_U16_LE((legacy_u8 *)&chunk->cursor.offset, pointer_offset);
			LEGACY_WRITE_U16_LE((legacy_u8 *)&chunk->cursor.segment, pointer_segment);
			resource_data_offset = LEGACY_U16_WRAP_ADD(resource_data_offset, 5U);
			LEGACY_WRITE_U16_LE((legacy_u8 *)&chunk->instruments.offset,
								LEGACY_U16_WRAP_ADD(resource_offset, 7U));
			LEGACY_WRITE_U16_LE((legacy_u8 *)&chunk->instruments.segment, resource_segment);
		} else {
			chunk->cursor.offset = 0;
			chunk->cursor.segment = 0;
		}

		channel = LEGACY_S16_WRAP_ADD(channel, 1);
	} while (channel <= last);
}

static void audio_clear_driver_context(struct AUDIO_CONTEXT *context)
{
	context->channel = AUDIO_CHANNEL_NONE;
	context->state = 0;
	context->priority = 0;
	context->resource.offset = 0;
	context->resource.segment = 0;
}

void audio_reset_channels(void)
{
	audio_update_lock = AUDIO_UPDATE_LOCKED;
	audio_init_channel_range(AUDIO_MUSIC_CHANNEL_FIRST, AUDIO_EFFECT_CHANNEL_LAST, 0, 0,
							 AUDIO_CHANNEL_DEFAULT_RATE, 0);
	struct AUDIO_CONTEXT *context = dos_audio_contexts;
	for (legacy_u16 context_index = 0; context_index < dos_audio_context_count; context_index++) {
		dos_audio_driver_release_channel((legacy_s16)context_index);
		audio_clear_driver_context(context);
		context->driver_channel = AUDIO_CHANNEL_NONE;
		context++;
	}
	dos_audio_driver_reset();
	dos_audio_driver_start();
	audio_update_lock = AUDIO_UPDATE_UNLOCKED;
}
void audio_release_channel_range(legacy_s16 first_channel, legacy_s16 last_channel)
{
	legacy_s16 channel;
	struct AUDIO_CHANNEL *chunk;
	struct AUDIO_CONTEXT *context;
	if (dos_audio_uses_direct_channels == 0) {
		context = dos_audio_contexts;
		for (legacy_u16 context_index = 0; context_index < dos_audio_context_count;
			 context_index++) {
			legacy_u16 channel_bits = context->channel;
			if (channel_bits >= (legacy_u16)first_channel &&
				channel_bits <= (legacy_u16)last_channel) {
				dos_audio_driver_release_channel((legacy_s16)context_index);
				audio_clear_driver_context(context);
			}
			context++;
		}
	} else {
		channel = first_channel;
		while (channel <= last_channel) {
			chunk = &audio_channels[channel];
			if (chunk->driver_channel < AUDIO_EFFECT_CHANNEL_FIRST) {
				dos_audio_driver_release_channel(chunk->driver_channel);
			}

			context = dos_audio_contexts;
			for (legacy_u16 context_index = 0; context_index < AUDIO_CONTEXT_COUNT;
				 context_index++) {
				if ((legacy_u16)context->channel == (legacy_u16)channel) {
					audio_clear_driver_context(context);
				}
				context++;
			}
			channel = LEGACY_S16_WRAP_ADD(channel, 1);
		}
	}

	channel = first_channel;
	while (channel <= last_channel) {
		chunk = &audio_channels[channel];
		chunk->active_notes = 0;
		channel = LEGACY_S16_WRAP_ADD(channel, 1);
	}
}

static void far *audio_select_sample_resource(void far *original_resource, legacy_u8 note)
{
	/* An instrument may be absent from the selected driver's sound bank. */
	if (original_resource == 0) {
		return 0;
	}
	if (((legacy_u8 far *)original_resource)[AUDIO_PERCUSSION_TYPE_OFFSET] !=
		AUDIO_PERCUSSION_TYPE) {
		return original_resource;
	}
	void far *resources[AUDIO_PERCUSSION_RESOURCE_COUNT];
	resources[0] = audio_bass_drum_resource;
	resources[1] = audio_snare_resource;
	resources[2] = audio_tom_resource;
	resources[3] = audio_ride_resource;
	resources[4] = audio_crash_resource;
	resources[5] = audio_closed_hihat_resource;
	resources[6] = audio_open_hihat_resource;
	legacy_u16 note_index = LEGACY_U16_WRAP_SUB(note, AUDIO_PERCUSSION_NOTE_FIRST);
	if (note_index >= AUDIO_PERCUSSION_NOTE_COUNT) {
		return audio_tom_resource;
	}
	static const legacy_u8 percussion_resource_indices[AUDIO_PERCUSSION_NOTE_COUNT] = {
		0U, 2U, 1U, 2U, 2U, 2U, 5U, 2U, 6U, 2U, 6U, 2U, 2U, 3U, 2U, 4U};
	return resources[percussion_resource_indices[note_index]];
}

struct AUDIO_CONTEXT_SELECTION {
	legacy_u32 oldest_state1_age;
	legacy_u32 oldest_state2_age;
	legacy_s16 oldest_state1;
	legacy_s16 oldest_state2;
};

static legacy_u8 audio_context_matches_resource(struct AUDIO_CONTEXT *context,
												struct AUDIO_CHANNEL *timer,
												legacy_u16 resource_mask, legacy_u16 context_index,
												legacy_s16 restrict_to_timer)
{
	if (dos_audio_uses_direct_channels == 0) {
		legacy_u16 context_mask =
			context_index < AUDIO_CONTEXT_COUNT ? (legacy_u16)(1U << context_index) : 0;
		if ((resource_mask & context_mask) == 0 ||
			(restrict_to_timer != 0 && timer->channel != context->channel)) {
			return 0;
		}
	}
	return 1;
}

static void audio_consider_older_context(struct AUDIO_CONTEXT *context, legacy_u16 context_index,
										 struct AUDIO_CONTEXT_SELECTION *selection)
{
	legacy_u32 age = context->age;
	if (context->state == AUDIO_CONTEXT_STATE_PLAYING && age > selection->oldest_state1_age) {
		selection->oldest_state1_age = age;
		selection->oldest_state1 = (legacy_s16)context_index;
	}
	if (context->state == AUDIO_CONTEXT_STATE_RELEASING && age > selection->oldest_state2_age) {
		selection->oldest_state2_age = age;
		selection->oldest_state2 = (legacy_s16)context_index;
	}
}

static void audio_reclaim_driver_context(legacy_s16 selected, struct AUDIO_CHANNEL *timer,
										 legacy_s16 restrict_to_timer)
{
	struct AUDIO_CONTEXT *context = &dos_audio_contexts[selected];
	struct AUDIO_CHANNEL *old_timer;
	if (dos_audio_uses_direct_channels == 0 && restrict_to_timer == 0) {
#if defined(RESTUNTS_SDL3) || defined(RESTUNTS_AMIGA)
		/* Native globals need not share one DOS data segment. */
		old_timer = &audio_channels[context->channel];
#else
		/* Recover the old record using its stored data-segment offset. */
		old_timer =
			(struct AUDIO_CHANNEL *)((legacy_u8 *)audio_timers +
									 LEGACY_U16_WRAP_SUB(context->timer_offset,
														 dos_memory_pointer_offset(audio_timers)));
#endif
		if (old_timer != timer) {
			old_timer->active_notes--;
			timer->active_notes++;
		}
	}
	dos_audio_driver_start_context(context->driver_channel, context);
	dos_audio_driver_end_context(context->driver_channel, context);
}

static legacy_s16 audio_find_driver_context(legacy_u8 far *resource, struct AUDIO_CHANNEL *timer)
{
	legacy_u16 resource_mask = resource_read_u16le(resource + AUDIO_RESOURCE_CONTEXT_MASK_OFFSET);
	if (resource_mask == 0) {
		return -1;
	}
#ifdef RESTUNTS_AMIGA
	/* AdLib percussion masks select FM channels 8 and 9. Every enabled
	 * instrument can use any of Paula's four voices (driver contexts 1..4). */
	resource_mask = 0x001eU;
#endif
	struct AUDIO_CONTEXT_SELECTION selection;
	selection.oldest_state1 = -1;
	selection.oldest_state2 = -1;
	selection.oldest_state1_age = 0;
	selection.oldest_state2_age = 0;

	legacy_s16 restrict_to_timer;
	legacy_u16 context_count;
	if (dos_audio_uses_direct_channels != 0) {
		context_count = AUDIO_CONTEXT_COUNT;
		restrict_to_timer = 0;
	} else {
		context_count = dos_audio_context_count;
		restrict_to_timer = timer->active_notes >= timer->note_limit;
	}

	struct AUDIO_CONTEXT *context = dos_audio_contexts;
	for (legacy_u16 context_index = 0; context_index < context_count; context_index++) {
		if (audio_context_matches_resource(context, timer, resource_mask, context_index,
										   restrict_to_timer) == 0) {
			context++;
			continue;
		}

		if (context->state == AUDIO_CONTEXT_STATE_FREE) {
			if (dos_audio_uses_direct_channels == 0) {
				timer->active_notes++;
			}
			return (legacy_s16)context_index;
		}
		if (dos_audio_uses_direct_channels == 0 && timer->priority < context->priority) {
			context++;
			continue;
		}

		audio_consider_older_context(context, context_index, &selection);
		context++;
	}

	legacy_s16 selected =
		selection.oldest_state2 != -1 ? selection.oldest_state2 : selection.oldest_state1;
	if (selected == -1) {
		return -1;
	}
	audio_reclaim_driver_context(selected, timer, restrict_to_timer);
	return selected;
}

legacy_s16 audio_start_note(struct AUDIO_CHANNEL *timer, legacy_u16 value, legacy_u32 duration,
							legacy_u8 note, legacy_u16 parameter, legacy_s16 handle)
{
	legacy_u8 far *resource =
		(legacy_u8 far *)audio_read_far_pointer((legacy_u8 *)&timer->resource);
	resource = (legacy_u8 far *)audio_select_sample_resource(resource, note);
	if (resource == 0) {
		return -1;
	}
	legacy_s16 context_index = audio_find_driver_context(resource, timer);
	if (context_index == -1) {
		return -1;
	}
	struct AUDIO_CONTEXT *context = &dos_audio_contexts[context_index];
	if (LEGACY_READ_U16_LE((const legacy_u8 *)&context->resource.offset) !=
			dos_memory_pointer_offset(resource) ||
		LEGACY_READ_U16_LE((const legacy_u8 *)&context->resource.segment) !=
			dos_memory_pointer_segment(resource)) {
		audio_write_far_pointer((legacy_u8 *)&context->resource, resource);
		if (dos_audio_uses_direct_channels == 0) {
			dos_audio_driver_prepare_context(context_index, context, (legacy_u8 *)timer, resource);
		}
	}

	context->channel = (legacy_u8)handle;
	context->state = AUDIO_CONTEXT_STATE_PLAYING;
	context->priority = timer->priority;
	context->age = 0;
	context->fade_out_flag = LEGACY_U32_WRAP_SUB(duration, 1UL);
	context->level =
		LEGACY_S16_FROM_BITS(resource_read_u16le(resource + AUDIO_RESOURCE_LEVEL_OFFSET));
	context->envelope_state = AUDIO_ENVELOPE_STATE_ATTACK;
	context->modulation_delay =
		resource_read_u16le(resource + AUDIO_RESOURCE_MODULATION_DELAY_OFFSET);
	context->modulation_count =
		resource_read_u16le(resource + AUDIO_RESOURCE_MODULATION_COUNT_OFFSET);
	context->modulation = 0;
	context->sequence_delay = resource_read_u16le(resource + AUDIO_RESOURCE_SEQUENCE_DELAY_OFFSET);
	context->sequence_count = resource_read_u16le(resource + AUDIO_RESOURCE_SEQUENCE_COUNT_OFFSET);
	context->sequence_value = 0;
	context->modulation_step =
		resource_read_u16le(resource + AUDIO_RESOURCE_MODULATION_STEP_OFFSET);
	context->modulation_direction = resource[AUDIO_RESOURCE_MODULATION_DIRECTION_OFFSET];
	context->modulation_tick = 0;
	context->sequence_tick = 0;
	context->sequence_index = 0;
	context->timer_offset = dos_memory_pointer_offset(timer);
	legacy_s16 driver_channel =
		dos_audio_uses_direct_channels == 0 ? context_index : timer->driver_channel;
	context->driver_channel = (legacy_u8)driver_channel;

	if (note == AUDIO_NOTE_USE_DRIVER_VALUE) {
		dos_audio_driver_set_context_value(driver_channel, context, value);
		if (dos_audio_uses_direct_channels != 0) {
			note = AUDIO_DIRECT_CHANNEL_DEFAULT_NOTE;
		}
	}
	legacy_s16 pitch =
		LEGACY_S16_WRAP_ADD((legacy_s8)resource[AUDIO_RESOURCE_BASE_PITCH_OFFSET], (legacy_s8)note);
	dos_audio_driver_activate_context(driver_channel, context, (legacy_u8 *)timer, pitch, parameter,
									  resource);
	audio_channel_notes[(legacy_u16)handle] = note;
	return context_index;
}

legacy_s16 audio_start_sample(legacy_u16 value, legacy_s16 handle)
{
	struct AUDIO_CHANNEL *timer = &audio_channels[handle];
	return audio_start_note(timer, value, AUDIO_SAMPLE_CONTINUOUS_DURATION,
							AUDIO_NOTE_USE_DRIVER_VALUE, 0, handle);
}

void audio_advance_driver_context(struct AUDIO_CONTEXT *context)
{
	context->age = LEGACY_U32_WRAP_ADD(context->age, 1UL);
	legacy_u32 value = context->fade_out_flag;
	if (value != 0) {
		context->fade_out_flag = LEGACY_U32_WRAP_SUB(value, 1UL);
		return;
	}

	dos_audio_driver_start_context(context->driver_channel, context);
	context->state = AUDIO_CONTEXT_STATE_RELEASING;
	struct AUDIO_CHANNEL *chunk = &audio_channels[context->channel];
	context->envelope_state =
		chunk->sustain != 0 ? AUDIO_ENVELOPE_STATE_SUSTAIN : AUDIO_ENVELOPE_STATE_RELEASE;
}

static legacy_u16 audio_absolute_word(legacy_s16 value)
{
	if (value < 0) {
		return (legacy_u16)LEGACY_S16_WRAP_NEGATE(value);
	}
	return (legacy_u16)value;
}

static void audio_update_context_envelope(struct AUDIO_CONTEXT *context, legacy_u8 far *resource)
{
	/* Successive envelope stages may run during this same tick. */
	legacy_s16 level;
	legacy_u16 value;
	if (context->envelope_state == AUDIO_ENVELOPE_STATE_ATTACK) {
		level =
			LEGACY_S16_WRAP_ADD(context->level, LEGACY_S16_FROM_BITS(resource_read_u16le(
													resource + AUDIO_RESOURCE_ATTACK_STEP_OFFSET)));
		context->level = level;
		value = resource_read_u16le(resource + AUDIO_RESOURCE_ATTACK_LEVEL_OFFSET);
		if (level >= LEGACY_S16_FROM_BITS(value)) {
			context->level = LEGACY_S16_FROM_BITS(value);
			context->envelope_state =
				LEGACY_S16_FROM_BITS(resource_read_u16le(
					resource + AUDIO_RESOURCE_SUSTAIN_LEVEL_OFFSET)) >= LEGACY_S16_FROM_BITS(value)
					? AUDIO_ENVELOPE_STATE_SUSTAIN
					: AUDIO_ENVELOPE_STATE_DECAY;
		}
	}
	if (context->envelope_state == AUDIO_ENVELOPE_STATE_DECAY) {
		level = LEGACY_S16_WRAP_SUB(
			context->level,
			LEGACY_S16_FROM_BITS(resource_read_u16le(resource + AUDIO_RESOURCE_DECAY_STEP_OFFSET)));
		context->level = level;
		value = resource_read_u16le(resource + AUDIO_RESOURCE_SUSTAIN_LEVEL_OFFSET);
		if (level <= LEGACY_S16_FROM_BITS(value)) {
			context->envelope_state = AUDIO_ENVELOPE_STATE_SUSTAIN;
			context->level = LEGACY_S16_FROM_BITS(value);
		}
	}
	if (context->envelope_state == AUDIO_ENVELOPE_STATE_SUSTAIN &&
		resource_read_u16le(resource + AUDIO_RESOURCE_SUSTAIN_LEVEL_OFFSET) == 0) {
		context->envelope_state = AUDIO_ENVELOPE_STATE_RELEASE;
	}
	struct AUDIO_CHANNEL *chunk;
	if (context->envelope_state == AUDIO_ENVELOPE_STATE_RELEASE) {
		level = LEGACY_S16_WRAP_SUB(context->level,
									LEGACY_S16_FROM_BITS(resource_read_u16le(
										resource + AUDIO_RESOURCE_RELEASE_STEP_OFFSET)));
		context->level = level;
		if (level <= 0) {
			context->level = 0;
			context->envelope_state = AUDIO_ENVELOPE_STATE_IDLE;
			context->state = AUDIO_CONTEXT_STATE_FREE;
			chunk = &audio_channels[context->channel];
			chunk->active_notes--;
			dos_audio_driver_end_context(context->driver_channel, context);
			audio_channel_notes[context->channel] = 0;
		}
	}
}

static void audio_update_context_modulation(struct AUDIO_CONTEXT *context, legacy_u8 far *resource)
{
	if (resource[AUDIO_RESOURCE_MODULATION_ENABLED_OFFSET] != 0) {
		legacy_u16 value = context->modulation_delay;
		if (value != 0) {
			context->modulation_delay = LEGACY_U16_WRAP_SUB(value, 1U);
		} else {
			value = context->modulation_count;
			if (value != 0) {
				if (value != AUDIO_MODULATION_COUNT_INFINITE) {
					context->modulation_count = LEGACY_U16_WRAP_SUB(value, 1U);
				}
				if (context->modulation_tick != 0) {
					context->modulation_tick--;
				} else {
					context->modulation_tick = resource[AUDIO_RESOURCE_MODULATION_TICK_OFFSET];
					legacy_s16 modulation = context->modulation;
					if (context->modulation_direction == AUDIO_MODULATION_DIRECTION_DECREASE) {
						modulation = LEGACY_S16_WRAP_SUB(modulation, context->modulation_step);
					} else {
						modulation = LEGACY_S16_WRAP_ADD(modulation, context->modulation_step);
					}
					context->modulation = modulation;
					legacy_u16 magnitude = audio_absolute_word(modulation);
					legacy_u16 threshold =
						resource_read_u16le(resource + AUDIO_RESOURCE_MODULATION_LIMIT_OFFSET);
					if (magnitude >= threshold) {
						if (context->modulation_direction == AUDIO_MODULATION_DIRECTION_DECREASE &&
							(resource[AUDIO_RESOURCE_MODULATION_DIRECTION_OFFSET] &
							 AUDIO_MODULATION_REVERSE_AT_MINIMUM) != 0) {
							context->modulation_direction = AUDIO_MODULATION_DIRECTION_INCREASE;
						} else if (context->modulation_direction !=
									   AUDIO_MODULATION_DIRECTION_DECREASE &&
								   (resource[AUDIO_RESOURCE_MODULATION_DIRECTION_OFFSET] &
									AUDIO_MODULATION_REVERSE_AT_MAXIMUM) != 0) {
							context->modulation_direction = AUDIO_MODULATION_DIRECTION_DECREASE;
						} else {
							context->modulation = 0;
						}
					}
				}
			}
		}
	}
}

static void audio_update_context_sequence(struct AUDIO_CONTEXT *context, legacy_u8 far *resource)
{
	if (resource[AUDIO_RESOURCE_SEQUENCE_ENABLED_OFFSET] != 0) {
		legacy_u16 value = context->sequence_delay;
		if (value != 0) {
			context->sequence_delay = LEGACY_U16_WRAP_SUB(value, 1U);
		} else {
			value = context->sequence_count;
			if (value != 0) {
				context->sequence_count = LEGACY_U16_WRAP_SUB(value, 1U);
				if (context->sequence_tick != 0) {
					context->sequence_tick--;
				} else {
					context->sequence_tick = resource[AUDIO_RESOURCE_SEQUENCE_TICK_OFFSET];
					legacy_u8 sequence_index = context->sequence_index++;
					context->sequence_value =
						resource[AUDIO_RESOURCE_SEQUENCE_VALUES_OFFSET +
								 (sequence_index & AUDIO_SEQUENCE_VALUE_INDEX_MASK)];
				}
			}
		}
	}
}

void audio_update_driver_contexts(void)
{
	struct AUDIO_CONTEXT *context = dos_audio_contexts;
	for (legacy_u16 context_index = 0; context_index < dos_audio_context_count; context_index++) {
		if (context->state == AUDIO_CONTEXT_STATE_FREE) {
			context++;
			continue;
		}
		if (context->channel > AUDIO_MUSIC_CHANNEL_LAST) {
			audio_advance_driver_context(context);
		}

		legacy_u8 far *resource =
			(legacy_u8 far *)audio_read_far_pointer((legacy_u8 *)&context->resource);
		audio_update_context_envelope(context, resource);

		audio_update_context_modulation(context, resource);

		audio_update_context_sequence(context, resource);

		dos_audio_driver_suspend_context(context->driver_channel, context, context->timer_offset,
										 resource);
		context++;
	}
	dos_audio_driver_suspend_all(dos_audio_contexts);
}

void audio_suspend(void)
{
	audio_suspended = AUDIO_STATE_ENABLED;
#ifdef RESTUNTS_SDL3
	sdl3_music_sync();
#endif
	audio_update_lock = AUDIO_UPDATE_LOCKED;
	if (dos_audio_uses_direct_channels != 0) {
		dos_audio_master_volume = 0;
		dos_audio_driver_set_master_state(AUDIO_DRIVER_MASTER_STATE_COMMAND,
										  (void far *)dos_audio_master_state);
		audio_update_lock = AUDIO_UPDATE_UNLOCKED;
		return;
	}

	struct AUDIO_CHANNEL *chunk = audio_channels;
	for (legacy_u16 channel = 0; channel < AUDIO_CHANNEL_COUNT; channel++) {
		if (audio_effects_enabled == AUDIO_STATE_ENABLED || channel < AUDIO_EFFECT_CHANNEL_FIRST) {
			audio_saved_channel_volumes[channel] = chunk->volume;
			dos_audio_set_channel_volume((legacy_s16)channel, 0);
		}
		chunk++;
	}

	struct AUDIO_CONTEXT *context = dos_audio_contexts;
	for (legacy_u16 context_index = 0; context_index < AUDIO_CONTEXT_COUNT; context_index++) {
		dos_audio_driver_suspend_context(context->driver_channel, context, context->timer_offset,
										 audio_read_far_pointer((legacy_u8 *)&context->resource));
		context++;
	}
	dos_audio_driver_suspend_all(dos_audio_contexts);
	audio_update_lock = AUDIO_UPDATE_UNLOCKED;
}

void audio_resume(void)
{
	audio_suspended = AUDIO_STATE_ENABLED;
	audio_update_lock = AUDIO_UPDATE_LOCKED;
	if (dos_audio_uses_direct_channels != 0) {
		dos_audio_master_volume = AUDIO_DEFAULT_MASTER_VOLUME;
		dos_audio_driver_set_master_state(AUDIO_DRIVER_MASTER_STATE_COMMAND,
										  (void far *)dos_audio_master_state);
	} else {
		for (legacy_u16 channel = 0; channel < AUDIO_CHANNEL_COUNT; channel++) {
			if (audio_effects_enabled == AUDIO_STATE_ENABLED ||
				channel < AUDIO_EFFECT_CHANNEL_FIRST) {
				dos_audio_set_channel_volume((legacy_s16)channel,
											 audio_saved_channel_volumes[channel]);
			}
		}
	}
	audio_update_lock = AUDIO_UPDATE_UNLOCKED;
	audio_suspended = AUDIO_STATE_DISABLED;
#ifdef RESTUNTS_SDL3
	sdl3_music_sync();
#endif
}

static legacy_s16 audio_find_free_sfx_channel(void)
{
	for (legacy_s16 candidate = AUDIO_EFFECT_CHANNEL_FIRST;
		 (legacy_u16)candidate <= AUDIO_EFFECT_CHANNEL_LAST; candidate++) {
		if ((audio_sfx_channels[candidate - AUDIO_EFFECT_CHANNEL_FIRST].cursor.offset |
			 audio_sfx_channels[candidate - AUDIO_EFFECT_CHANNEL_FIRST].cursor.segment) == 0 &&
			audio_channel_reserved[candidate] == 0) {
			return candidate;
		}
	}
	return -1;
}

legacy_s16 audio_play_effect_at_rate(void far *resource, legacy_s16 channel, legacy_u8 priority,
									 legacy_u16 rate)
{
	const legacy_u8 far *bytes = (const legacy_u8 far *)resource;
	if (audio_effects_enabled == AUDIO_STATE_DISABLED || resource == 0 ||
		bytes[AUDIO_EFFECT_RESOURCE_TYPE_OFFSET] != AUDIO_RESOURCE_TYPE_EFFECT) {
		return -1;
	}

	if (audio_effect_rate != 0) {
		legacy_u16 scaled_rate =
			(legacy_u16)((legacy_u32)(legacy_u16)rate * AUDIO_EFFECT_RATE_SCALE);
		rate = LEGACY_U16_WRAP_SUB(
			LEGACY_U16_DIV_OR_ZERO(scaled_rate, (legacy_u16)audio_effect_rate), 1U);
	} else {
		rate = 0;
	}

	if (channel == -1) {
		channel = audio_find_free_sfx_channel();
	}
	if (channel == -1) {
		legacy_u8 lowest_priority = AUDIO_PRIORITY_LOWEST;
		legacy_s16 replacement = -1;
		for (legacy_s16 candidate = AUDIO_EFFECT_CHANNEL_FIRST;
			 (legacy_u16)candidate <= AUDIO_EFFECT_CHANNEL_LAST; candidate++) {
			if (audio_channel_reserved[candidate] == 0 &&
				audio_channels[candidate].priority <= lowest_priority) {
				lowest_priority = audio_channels[candidate].priority;
				replacement = candidate;
			}
		}
		if (replacement != -1 && lowest_priority <= priority) {
			audio_stop_effect_channel(replacement);
			channel = replacement;
		}
	}

	if (channel == -1) {
		return -1;
	}

	legacy_u16 resource_data_offset = (legacy_u16)bytes[AUDIO_EFFECT_RESOURCE_ENTRY_COUNT_OFFSET] *
										  AUDIO_EFFECT_RESOURCE_ENTRY_SIZE +
									  AUDIO_EFFECT_RESOURCE_TABLE_OFFSET;
	audio_init_channel_range(channel, channel, resource, resource_data_offset, rate, priority);
	return channel;
}

legacy_s16 audio_play_effect(void far *resource, legacy_s16 channel, legacy_u8 priority)
{
	return audio_play_effect_at_rate(resource, channel, priority, (legacy_u16)audio_effect_rate);
}

legacy_s16 audio_play_default_effect(void far *resource)
{
	return audio_play_effect(resource, -1, AUDIO_EFFECT_DEFAULT_PRIORITY);
}

legacy_s16 audio_reserve_effect_channel(legacy_s16 channel, legacy_u8 priority)
{
	if (channel == -1) {
		channel = audio_find_free_sfx_channel();
	}

	if (channel != -1) {
		audio_channel_reserved[channel] = 1;
		audio_channels[channel].priority = priority;
	}

	return channel;
}

void audio_stop_effect_channel(legacy_s16 channel)
{
	if ((legacy_u16)channel < AUDIO_EFFECT_CHANNEL_FIRST ||
		(legacy_u16)channel > AUDIO_EFFECT_CHANNEL_LAST) {
		return;
	}

	audio_channels[channel].cursor.offset = 0;
	audio_channels[channel].cursor.segment = 0;
	audio_release_channel_range(channel, channel);
	audio_init_channel_range(channel, channel, 0, 0, audio_effect_rate, 0);
}

void audio_stop_skid_sound(legacy_s16 index)
{
	legacy_s16 channel = audio_timers[index].secondary_effect_channel;
	audio_stop_effect_channel(channel);
	audio_timers[index].secondary_effect_channel = -1;
}

legacy_s16 audio_timer_effect_idle(legacy_s16 index)
{
	legacy_s16 channel = audio_timers[index].effect_channel;
	if (channel < 0) {
		return 1;
	}

	return audio_effect_channel_idle(channel);
}

void audio_stop_engine(legacy_s16 index)
{
	if (audio_timers[index].active != 1 || audio_timers[index].engine_active != 1) {
		return;
	}

	legacy_s16 channel = audio_timers[index].engine_context;
	audio_request_context_fade(channel);
	audio_timers[index].engine_context = -1;
	audio_timers[index].engine_active = 0;
	audio_timers[index].parameters_changed = 1;
}

static legacy_s16 audio_start_timer_resource(struct AUDIO_TIMER *timer, legacy_u16 resource_index,
											 legacy_u8 priority)
{
	legacy_u16 rate = timer->current_volume >> AUDIO_TIMER_INTERPOLATION_FRACTION_BITS;
	void far *resource =
		audio_read_far_pointer((legacy_u8 *)&timer->definition.resources[resource_index]);
	return audio_play_effect_at_rate(resource, -1, priority, rate);
}

static legacy_s16 audio_start_indexed_event(legacy_s16 index, legacy_u16 resource_index,
											legacy_u8 priority)
{
	struct AUDIO_TIMER *timer = &audio_timers[index];
	legacy_s16 channel = audio_start_timer_resource(timer, resource_index, priority);
	timer->effect_channel = channel;
	timer->parameters_changed = 1;
	return channel;
}

void audio_play_engine_start(legacy_s16 index)
{
	audio_start_indexed_event(index, 2U, AUDIO_EFFECT_DEFAULT_PRIORITY);
	audio_timers[index].restart_engine = 1;
}

void audio_play_bump(legacy_s16 index)
{
	audio_start_indexed_event(index, 8U, AUDIO_EFFECT_DEFAULT_PRIORITY);
}

void audio_play_scrape(legacy_s16 index)
{
	audio_start_indexed_event(index, 9U, AUDIO_EFFECT_DEFAULT_PRIORITY);
}

void audio_play_crash_and_stop_engine(legacy_s16 index)
{
	audio_start_indexed_event(index, 5U, AUDIO_EFFECT_HIGH_PRIORITY);
	audio_stop_engine(index);
}

void audio_play_engine_stop(legacy_s16 index)
{
	audio_start_indexed_event(index, 3U, AUDIO_EFFECT_DEFAULT_PRIORITY);
	audio_stop_engine(index);
}

void audio_play_engine_failure(legacy_s16 index)
{
	audio_start_indexed_event(index, 4U, AUDIO_EFFECT_DEFAULT_PRIORITY);
	audio_stop_engine(index);
}

static void audio_start_secondary_event(legacy_s16 index, legacy_u16 resource_index)
{
	struct AUDIO_TIMER *timer = &audio_timers[index];
	legacy_s16 channel = timer->secondary_effect_channel;
	if (channel != -1) {
		audio_stop_effect_channel(channel);
	}

	channel = audio_start_timer_resource(timer, resource_index, AUDIO_EFFECT_DEFAULT_PRIORITY);
	timer->secondary_effect_channel = channel;
	timer->parameters_changed = 1;
}

void audio_play_paved_skid(legacy_s16 index)
{
	audio_start_secondary_event(index, 6U);
}

void audio_play_offroad_skid(legacy_s16 index)
{
	audio_start_secondary_event(index, 7U);
}

void audio_release_effect_channel(legacy_s16 channel)
{
	if (channel > -1) {
		audio_channel_reserved[channel] = 0;
		audio_stop_effect_channel(channel);
	}
}

void audio_request_context_fade(legacy_s16 index)
{
	dos_audio_contexts[index].fade_out_flag = 1;
}

legacy_s16 audio_set_music_channel_volume(legacy_s16 value)
{
	legacy_s16 channel;
#ifdef RESTUNTS_SDL3
	sdl3_music_set_volume(value);
#endif

	for (channel = 0; channel < (legacy_u16)audio_music_channel_count; channel++) {
		dos_audio_set_channel_volume(channel, value);
	}

	return channel;
}

void audio_fade_out(legacy_s16 delay_ticks)
{
	legacy_u32 delay = (legacy_u32)(legacy_s32)delay_ticks;
	legacy_u8 uses_direct_channels = dos_audio_uses_direct_channels;
	legacy_s16 volume = uses_direct_channels != 0 ? AUDIO_DEFAULT_MASTER_VOLUME : audio_music_rate;
	while (volume > 0) {
		audio_update_lock = AUDIO_UPDATE_LOCKED;
		if (uses_direct_channels != 0) {
			dos_audio_master_volume = (legacy_u8)volume;
			dos_audio_driver_set_master_state(AUDIO_DRIVER_MASTER_STATE_COMMAND,
											  (void far *)dos_audio_master_state);
		} else {
			audio_set_music_channel_volume(volume);
		}
		audio_update_lock = AUDIO_UPDATE_UNLOCKED;
		timer_set_deadline(delay);
		timer_wait_for_deadline();
		volume = LEGACY_S16_WRAP_SUB(volume, AUDIO_FADE_VOLUME_STEP);
	}

	audio_stop_music();
	if (dos_audio_uses_direct_channels != 0) {
		timer_set_deadline(AUDIO_FADE_RESTORE_DELAY_TICKS);
		timer_wait_for_deadline();
		dos_audio_master_volume = AUDIO_DEFAULT_MASTER_VOLUME;
		dos_audio_driver_set_master_state(AUDIO_DRIVER_MASTER_STATE_COMMAND,
										  (void far *)dos_audio_master_state);
	}
}

legacy_s16 audio_set_music_volume(legacy_s16 value)
{
	audio_music_rate = (legacy_u8)value;
	return audio_set_music_channel_volume(value);
}

legacy_u16 audio_get_channel_value(legacy_s16 channel)
{
	return (legacy_u16)audio_channel_values[(legacy_u16)channel];
}

legacy_u16 audio_get_channel_note(legacy_s16 channel)
{
	return (legacy_u16)audio_channel_notes[(legacy_u16)channel];
}

void audio_play_car_events(legacy_u8 flags, legacy_s16 channel)
{
	if (audio_car_state_ready == 0) {
		return;
	}

	if ((flags & AUDIO_CAR_EVENT_SCRAPE_FLAG) != 0) {
		audio_play_scrape(channel);
	}
	if ((flags & AUDIO_CAR_EVENT_BUMP_FLAG) != 0) {
		audio_play_bump(channel);
	}
}
