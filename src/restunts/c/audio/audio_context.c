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
