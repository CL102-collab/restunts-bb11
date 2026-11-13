#include <stddef.h>
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
#include "../../platform/sdl3/audio_commands.h"
#endif
#include "audio.h"
#include "audio_internal.h"
#include "../externs.h"
#include "../fileio.h"
#include "../legacy.h"
#include "../memmgr.h"
#include "../platform.h"
#include "../resource.h"
#include "../timing.h"
#include "../fatal.h"
#include "../car/car_audio.h"
#include "audio_control.h"
#include "../resource_bytes.h"
#ifdef RESTUNTS_SDL3
#include "../../platform/sdl3/music.h"
#endif

#include "audio_engine_internal.h"

legacy_u8 audio_music_channel_count;
legacy_u8 audio_suspended;
legacy_u8 audio_music_active;
legacy_u8 audio_music_rate;
legacy_s8 audio_car_state_ready;
legacy_s8 audio_player_car_flags;
legacy_s8 audio_opponent_car_flags;
legacy_u8 audio_effect_rate;
legacy_u8 audio_channel_reserved[AUDIO_CHANNEL_COUNT];
void far *audio_bass_drum_resource;
void far *audio_snare_resource;
void far *audio_tom_resource;
void far *audio_ride_resource;
void far *audio_crash_resource;
void far *audio_closed_hihat_resource;
void far *audio_open_hihat_resource;
legacy_s16 audio_player_engine_channel;
legacy_s16 audio_opponent_engine_channel;
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
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	AUDIO_ASYNC0(AUDIO_COMMAND_ADD_DRIVER_TIMER);
#endif
	for (legacy_u16 index = 0; index < AUDIO_TIMER_COUNT; index++) {
		audio_timers[index].active = 0;
	}
	timer_reg_callback(&audio_driver_timer);
}

void audio_remove_driver_timer(void)
{
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	AUDIO_ASYNC0(AUDIO_COMMAND_REMOVE_DRIVER_TIMER);
#endif
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
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	if (sdl3_audio_dispatch_needed()) {
		legacy_s16 result = sdl3_audio_command_engine(unused_type, source_pointer, shape_resources,
													  audio_resources);
		if (result < 0) {
			fatal_error("Cannot initialize audio engine.");
		}
		return result;
	}
#endif
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
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	AUDIO_ASYNC1(AUDIO_COMMAND_START_ENGINE, index);
#endif
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
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	if (sdl3_audio_dispatch_needed()) {
		const legacy_s16 values[] = {index,	   base_value, first_x,	 first_y, first_z,
									 second_x, second_y,   second_z, interval};
		sdl3_audio_command_spatial(values);
		return;
	}
#endif
#if defined(RESTUNTS_AUDIO_WORKER) && !defined(__DJGPP__)
	if ((legacy_u16)index >= AUDIO_TIMER_COUNT || !audio_timers[index].active) {
		return;
	}
#endif
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
