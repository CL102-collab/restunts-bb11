#include <stddef.h>
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

static legacy_u8 audio_saved_channel_volumes[AUDIO_CHANNEL_COUNT];
static legacy_u8 audio_effect_channel_volumes[AUDIO_CHANNEL_COUNT];
legacy_u8 audio_channel_values[AUDIO_CHANNEL_COUNT];
legacy_u8 audio_channel_notes[AUDIO_CHANNEL_COUNT];

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

void audio_clear_driver_context(struct AUDIO_CONTEXT *context)
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

static void audio_start_impact_event(legacy_s16 index, legacy_u16 resource_index,
									 legacy_u8 priority)
{
	if (audio_start_indexed_event(index, resource_index, priority) < 0) {
		return;
	}
#ifdef RESTUNTS_SDL3
	/* Replay Play clears is_in_replay; pause and seeking set it to suppress audio. */
	if (index == audio_player_engine_channel && is_in_replay == 0 &&
		state.game_inputmode == GAME_INPUT_MODE_ACTIVE && audio_suspended == AUDIO_STATE_DISABLED) {
		sdl3_car_impact_feedback();
	}
#endif
}

void audio_play_bump(legacy_s16 index)
{
	audio_start_impact_event(index, 8U, AUDIO_EFFECT_DEFAULT_PRIORITY);
}

void audio_play_scrape(legacy_s16 index)
{
	audio_start_impact_event(index, 9U, AUDIO_EFFECT_DEFAULT_PRIORITY);
}

void audio_play_crash_and_stop_engine(legacy_s16 index)
{
	audio_start_impact_event(index, 5U, AUDIO_EFFECT_HIGH_PRIORITY);
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
