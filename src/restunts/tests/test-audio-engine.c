#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../c/audio_engine.c"
#undef printf

struct AUDIO_TIMER audio_timers[AUDIO_TIMER_COUNT];
struct AUDIO_CHANNEL audio_channels[AUDIO_CHANNEL_COUNT];
struct AUDIO_CHANNEL *audio_sfx_channels = &audio_channels[AUDIO_EFFECT_CHANNEL_FIRST];
struct AUDIO_CONTEXT dos_audio_contexts[AUDIO_CONTEXT_COUNT];
legacy_u8 dos_audio_context_count, dos_audio_uses_direct_channels;
legacy_s8 audio_effects_enabled;
static legacy_u8 memory[1048576];
static legacy_u8 segments_match;
static legacy_u64 trace_hash = UINT64_C(1469598103934665603);
#ifdef RESTUNTS_SDL3
struct GAMESTATE state;
legacy_s8 is_in_replay;
legacy_u8 game_replay_mode;
static legacy_uint impact_feedback_count;
void sdl3_car_impact_feedback(void)
{
	impact_feedback_count++;
}
#endif
static void trace_word(legacy_u16 value)
{
	trace_hash = (trace_hash ^ (value & 255U)) * UINT64_C(1099511628211);
	trace_hash = (trace_hash ^ (value >> 8)) * UINT64_C(1099511628211);
}
static void trace_bytes(const void *data, legacy_u32 count)
{
	const legacy_u8 *p = data;
	for (legacy_u32 i = 0; i < count; i++) {
		trace_word(p[i]);
	}
}
void *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	return memory + (((legacy_u32)segment << 4) + offset) % sizeof(memory);
}
legacy_u16 dos_memory_pointer_offset(const void *pointer)
{
	uintptr_t p = (uintptr_t)pointer;
	if (p >= (uintptr_t)memory && p < (uintptr_t)(memory + sizeof(memory))) {
		return (legacy_u16)(p - (uintptr_t)memory);
	}
	if (p >= (uintptr_t)audio_timers && p < (uintptr_t)(audio_timers + AUDIO_TIMER_COUNT)) {
		return (legacy_u16)(0x1000 + p - (uintptr_t)audio_timers);
	}
	if (p >= (uintptr_t)audio_channels && p < (uintptr_t)(audio_channels + AUDIO_CHANNEL_COUNT)) {
		return (legacy_u16)(0x3000 + p - (uintptr_t)audio_channels);
	}
	assert(pointer == 0);
	return 0;
}
legacy_u16 dos_memory_pointer_segment(const void *pointer)
{
	uintptr_t p = (uintptr_t)pointer;
	if (p >= (uintptr_t)memory && p < (uintptr_t)(memory + sizeof(memory))) {
		return (legacy_u16)(((p - (uintptr_t)memory) >> 16) << 12);
	}
	return 0;
}
legacy_s16 dos_data_stack_segments_match(void)
{
	trace_word(1);
	return segments_match;
}
static void trace_context(legacy_u32 call, legacy_s16 channel, struct AUDIO_CONTEXT *context)
{
	trace_word(call);
	trace_word(channel);
	trace_bytes(context, sizeof(*context));
}
void dos_audio_driver_start_context(legacy_s16 channel, struct AUDIO_CONTEXT *context)
{
	trace_context(2, channel, context);
}
void dos_audio_driver_end_context(legacy_s16 channel, struct AUDIO_CONTEXT *context)
{
	trace_context(3, channel, context);
}
void dos_audio_driver_suspend_context(legacy_s16 channel, struct AUDIO_CONTEXT *context,
									  legacy_u16 value, void *resource)
{
	trace_context(4, channel, context);
	trace_word(value);
	trace_word(dos_memory_pointer_offset(resource));
}
void dos_audio_driver_suspend_all(struct AUDIO_CONTEXT *contexts)
{
	trace_word(5);
	assert(contexts == dos_audio_contexts);
}
void dos_audio_set_channel_volume(legacy_s16 channel, legacy_s16 volume)
{
	trace_word(6);
	trace_word(channel);
	trace_word(volume);
}
void dos_audio_set_context_pitch(legacy_s16 index, legacy_s16 pitch)
{
	trace_word(7);
	trace_word(index);
	trace_word(pitch);
}
void dos_audio_bind_channel_context(legacy_s16 channel, void *resource)
{
	trace_word(8);
	trace_word(channel);
	audio_write_far_pointer((legacy_u8 *)&audio_channels[channel].resource, resource);
}
void dos_audio_driver_prepare_context(legacy_s16 channel, struct AUDIO_CONTEXT *context,
									  legacy_u8 *timer, void *resource)
{
	(void)timer;
	(void)resource;
	trace_context(9, channel, context);
}
void dos_audio_driver_set_context_value(legacy_s16 channel, struct AUDIO_CONTEXT *context,
										legacy_u16 value)
{
	trace_context(10, channel, context);
	trace_word(value);
}
void dos_audio_driver_activate_context(legacy_s16 channel, struct AUDIO_CONTEXT *context,
									   legacy_u8 *timer, legacy_s16 pitch, legacy_u16 parameter,
									   void *resource)
{
	(void)timer;
	(void)resource;
	trace_context(11, channel, context);
	trace_word(pitch);
	trace_word(parameter);
}
void dos_audio_driver_release_channel(legacy_s16 channel)
{
	trace_word(12);
	trace_word(channel);
}
legacy_s8 *pad_id(const legacy_s8 *source)
{
	(void)source;
	trace_word(13);
	return (legacy_s8 *)"TEST";
}
legacy_s8 *locate_shape_fatal(legacy_s8 *data, const legacy_s8 *name)
{
	(void)data;
	(void)name;
	trace_word(14);
	return (legacy_s8 *)(memory + 0x50000);
}
void *init_audio_resources(void *song, void *voice, const legacy_s8 *name)
{
	(void)song;
	(void)voice;
	(void)name;
	trace_word(15);
	return memory + 0x50000;
}
void fatal_error(const legacy_s8 *format, ...)
{
	(void)format;
	trace_word(16);
}
legacy_u16 resource_read_u16le(const legacy_u8 *p)
{
	return LEGACY_READ_U16_LE(p);
}
legacy_u32 resource_read_u32le(const legacy_u8 *p)
{
	return (legacy_u32)LEGACY_READ_U16_LE(p) | ((legacy_u32)LEGACY_READ_U16_LE(p + 2) << 16);
}
static void reset_engine(legacy_u32 index)
{
	memset(audio_timers, 0, sizeof(audio_timers));
	memset(audio_channels, 0, sizeof(audio_channels));
	memset(dos_audio_contexts, 0, sizeof(dos_audio_contexts));
	memset(audio_channel_notes, 0, sizeof(audio_channel_notes));
	memset(audio_channel_reserved, 0, sizeof(audio_channel_reserved));
	memset(memory + 0x50000, 0, 128);
	dos_audio_context_count = 8;
	dos_audio_uses_direct_channels = 0;
	audio_effects_enabled = 1;
	segments_match = 1;
	audio_driver_timer_divider = 0;
	audio_effect_rate = 0;
	trace_word(index);
}
#ifndef RESTUNTS_SDL3
static void test_envelopes(void)
{
	legacy_u8 *r = memory + 0x50000;
	struct AUDIO_CONTEXT *c = &dos_audio_contexts[0];
	static const legacy_s16 levels[] = {0, 1, 127, 32760, -32760, -1};
	for (legacy_u32 i = 0; i < 192U; i++) {
		reset_engine(i);
		dos_audio_context_count = 1;
		c->state = i % 16U == 0 ? 0 : 1;
		c->channel = i % 2U ? 16 : 0;
		c->driver_channel = 4;
		c->age = 0xfffffffeUL;
		c->fade_out_flag = i % 3U;
		c->level = levels[i % 6U];
		c->envelope_state = i % 5U;
		c->modulation_delay = i % 3U;
		c->modulation_count = i % 4U == 0 ? 32767 : i % 4U;
		c->modulation = levels[(i + 2U) % 6U];
		c->modulation_step = i % 2U ? 1 : 32767;
		c->modulation_direction = i % 3U;
		c->modulation_tick = i % 2U;
		c->sequence_delay = i % 2U;
		c->sequence_count = 3;
		c->sequence_tick = i % 2U;
		c->sequence_index = 255;
		audio_channels[c->channel].active_notes = 1;
		audio_channels[c->channel].sustain = i % 3U == 0;
		audio_channel_notes[c->channel] = 60;
		audio_write_far_pointer((legacy_u8 *)&c->resource, r);
		LEGACY_WRITE_U16_LE(r + 30, 127);
		LEGACY_WRITE_U16_LE(r + 32, i % 2U ? 32767 : 1);
		LEGACY_WRITE_U16_LE(r + 34, 128);
		LEGACY_WRITE_U16_LE(r + 36, i % 3U ? 0 : 127);
		LEGACY_WRITE_U16_LE(r + 38, 1);
		r[40] = i % 5U != 0;
		r[41] = 1;
		LEGACY_WRITE_U16_LE(r + 46, i % 4U ? 127 : 0);
		r[52] = i % 4U;
		r[53] = i % 7U != 0;
		r[58] = 1;
		for (legacy_u32 tick = 0; tick < 8U; tick++) {
			r[59 + tick] = (legacy_u8)(17U * tick);
		}
		for (legacy_u32 tick = 0; tick < 4U; tick++) {
			audio_update_driver_contexts();
			trace_bytes(c, sizeof(*c));
			trace_word(audio_channels[c->channel].active_notes);
			trace_word(audio_channel_notes[c->channel]);
		}
	}
	reset_engine(250);
	dos_audio_context_count = 1;
	c->state = 1;
	c->channel = 0;
	c->envelope_state = 1;
	audio_channels[0].active_notes = 1;
	audio_write_far_pointer((legacy_u8 *)&c->resource, r);
	LEGACY_WRITE_U16_LE(r + 30, 1);
	LEGACY_WRITE_U16_LE(r + 32, 1);
	LEGACY_WRITE_U16_LE(r + 34, 1);
	LEGACY_WRITE_U16_LE(r + 38, 1);
	audio_update_driver_contexts();
	trace_bytes(c, sizeof(*c));
}
static void test_context_selection(void)
{
	legacy_u8 *r = memory + 0x50000;
	struct AUDIO_CHANNEL *timer;
	struct AUDIO_CHANNEL *old;
	for (legacy_u32 i = 0; i < 128U; i++) {
		reset_engine(300U + i);
		timer = (struct AUDIO_CHANNEL *)((legacy_u8 *)audio_timers + 76);
		old = (struct AUDIO_CHANNEL *)audio_timers;
		timer->channel = 17;
		timer->priority = i % 3U;
		timer->note_limit = i % 2U;
		timer->active_notes = i % 4U;
		old->active_notes = 4;
		dos_audio_uses_direct_channels = i % 2U;
		dos_audio_context_count = i % 3U ? 8 : 0;
		LEGACY_WRITE_U16_LE(r + 12, i % 5U == 0	  ? 0
									: i % 5U == 1 ? 1
									: i % 5U == 2 ? 0xaaaa
												  : 65535);
		for (legacy_u32 j = 0; j < 16U; j++) {
			dos_audio_contexts[j].channel = j % 3U ? 17 : 18;
			dos_audio_contexts[j].state = (i + j) % 7U == 0 ? 0 : (j % 2U) + 1U;
			dos_audio_contexts[j].priority = j % 3U;
			dos_audio_contexts[j].age = i % 3U == 0 ? 0 : i % 3U == 1 ? 10 : j;
			dos_audio_contexts[j].driver_channel = j;
			dos_audio_contexts[j].timer_offset = dos_memory_pointer_offset(i % 4U ? old : timer);
		}
		trace_word(audio_find_driver_context(r, timer));
		trace_word(timer->active_notes);
		trace_word(old->active_notes);
	}
}
static void test_engine_initialization(void)
{
	struct AUDIO_ENGINE_DEFINITION *source = (struct AUDIO_ENGINE_DEFINITION *)(memory + 0x2fff0);
	for (legacy_u32 i = 0; i < 4U; i++) {
		reset_engine(500U + i);
		memset(source, 0, sizeof(*source));
		source->sample_count = i % 2U ? 65535 : 127;
		source->initialized = i % 2U;
		audio_write_far_pointer((legacy_u8 *)&source->resources[0], memory + 0x50000);
		memory[0x5000e] = i % 2U;
		memory[0x5000f] = 255;
		if (i == 3U) {
			for (legacy_u32 j = 0; j < AUDIO_TIMER_COUNT; j++) {
				audio_timers[j].active = 1;
			}
		}
		trace_word(audio_init_engine(33, source, memory + 0x60000, memory + 0x70000));
		trace_bytes(audio_timers, sizeof(audio_timers));
	}
}
static void test_driver_timer(void)
{
	struct AUDIO_TIMER *timer = &audio_timers[0];
	for (legacy_u32 i = 0; i < 64U; i++) {
		reset_engine(600U + i);
		audio_driver_timer_divider = i % 3U;
		dos_audio_uses_direct_channels = i % 2U;
		segments_match = i % 8U != 0;
		audio_effects_enabled = i % 7U != 0;
		timer->active = i % 6U != 0;
		timer->channel = 16;
		timer->current_volume = (legacy_u16)(i * 1003U);
		timer->target_volume = (legacy_u8)(i * 31U);
		timer->current_pitch = 0xfffffff0UL;
		timer->target_pitch = i * 1009U;
		timer->last_pitch = i * 2003U;
		timer->engine_context = i % 3U == 0 ? -1 : 0;
		timer->effect_channel = i % 3U == 0 ? -1 : 17;
		timer->secondary_effect_channel = i % 4U ? -1 : 18;
		timer->last_volume = i * 2U;
		timer->parameters_changed = i % 2U;
		timer->restart_engine = i % 2U;
		timer->engine_active = i % 3U == 0;
		timer->definition.sample_count = 600;
		memory[0x5000e] = 2;
		LEGACY_WRITE_U16_LE(memory + 0x5000c, 65535);
		audio_write_far_pointer((legacy_u8 *)&timer->definition.resources[0], memory + 0x50000);
		for (legacy_u32 j = 0; j < 3U; j++) {
			audio_driver_timer();
			trace_bytes(timer, sizeof(*timer));
			trace_word(audio_driver_timer_divider);
		}
	}
}
#endif

#ifdef RESTUNTS_SDL3
static void reset_impact_sound(void)
{
	reset_engine(700);
	impact_feedback_count = 0;
	audio_player_engine_channel = 0;
	audio_opponent_engine_channel = 1;
	audio_car_state_ready = 1;
	audio_suspended = AUDIO_STATE_DISABLED;
	game_replay_mode = REPLAY_MODE_LIVE;
	is_in_replay = 0;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	memory[0x50000 + AUDIO_EFFECT_RESOURCE_TYPE_OFFSET] = AUDIO_RESOURCE_TYPE_EFFECT;
	for (legacy_u16 index = 0; index < 2; index++) {
		audio_timers[index].current_volume = 100U << AUDIO_TIMER_INTERPOLATION_FRACTION_BITS;
		for (legacy_u16 resource = 2; resource < 10; resource++) {
			audio_write_far_pointer(
				(legacy_u8 *)&audio_timers[index].definition.resources[resource], memory + 0x50000);
		}
	}
}

static void play_impact_sound(legacy_u16 event, legacy_s16 channel)
{
	if (event == 0) {
		audio_play_car_events(AUDIO_CAR_EVENT_BUMP_FLAG, channel);
	} else if (event == 1) {
		audio_play_car_events(AUDIO_CAR_EVENT_SCRAPE_FLAG, channel);
	} else {
		audio_play_crash_and_stop_engine(channel);
	}
}

static void test_impact_feedback(void)
{
	static const legacy_u8 modes[] = {REPLAY_MODE_LIVE, REPLAY_MODE_PLAYBACK};
	for (legacy_u16 mode = 0; mode < sizeof(modes) / sizeof(modes[0]); mode++) {
		for (legacy_u16 event = 0; event < 3; event++) {
			reset_impact_sound();
			game_replay_mode = modes[mode];
			/* Playing a replay uses is_in_replay == 0, just like live driving. */
			play_impact_sound(event, audio_player_engine_channel);
			assert(impact_feedback_count == 1);
			assert(audio_timers[audio_player_engine_channel].effect_channel >= 0);

			reset_impact_sound();
			game_replay_mode = modes[mode];
			play_impact_sound(event, audio_opponent_engine_channel);
			assert(audio_timers[audio_opponent_engine_channel].effect_channel >= 0);
			assert(impact_feedback_count == 0);

			reset_impact_sound();
			game_replay_mode = modes[mode];
			audio_effects_enabled = AUDIO_STATE_DISABLED;
			play_impact_sound(event, audio_player_engine_channel);
			assert(audio_timers[audio_player_engine_channel].effect_channel == -1);
			assert(impact_feedback_count == 0);

			reset_impact_sound();
			game_replay_mode = modes[mode];
			for (legacy_u16 channel = AUDIO_EFFECT_CHANNEL_FIRST;
				 channel <= AUDIO_EFFECT_CHANNEL_LAST; channel++) {
				audio_channel_reserved[channel] = 1;
			}
			play_impact_sound(event, audio_player_engine_channel);
			assert(audio_timers[audio_player_engine_channel].effect_channel == -1);
			assert(impact_feedback_count == 0);

			reset_impact_sound();
			game_replay_mode = modes[mode];
			memory[0x50000 + AUDIO_EFFECT_RESOURCE_TYPE_OFFSET] = 0;
			play_impact_sound(event, audio_player_engine_channel);
			assert(audio_timers[audio_player_engine_channel].effect_channel == -1);
			assert(impact_feedback_count == 0);

			reset_impact_sound();
			game_replay_mode = modes[mode];
			/* Paused, scrubbed, and reconstructed frames suppress feedback. */
			is_in_replay = 1;
			play_impact_sound(event, audio_player_engine_channel);
			assert(impact_feedback_count == 0);

			reset_impact_sound();
			game_replay_mode = modes[mode];
			state.game_inputmode = GAME_INPUT_MODE_INTRO;
			play_impact_sound(event, audio_player_engine_channel);
			assert(impact_feedback_count == 0);

			reset_impact_sound();
			game_replay_mode = modes[mode];
			state.game_inputmode = GAME_INPUT_MODE_WAITING;
			play_impact_sound(event, audio_player_engine_channel);
			assert(impact_feedback_count == 0);

			reset_impact_sound();
			game_replay_mode = modes[mode];
			audio_suspended = AUDIO_STATE_ENABLED;
			play_impact_sound(event, audio_player_engine_channel);
			assert(impact_feedback_count == 0);
		}
	}

	reset_impact_sound();
	audio_play_car_events(0, audio_player_engine_channel);
	assert(impact_feedback_count == 0);
	audio_car_state_ready = 0;
	audio_play_car_events(AUDIO_CAR_EVENT_BUMP_FLAG | AUDIO_CAR_EVENT_SCRAPE_FLAG,
						  audio_player_engine_channel);
	assert(impact_feedback_count == 0);

	reset_impact_sound();
	audio_play_car_events(AUDIO_CAR_EVENT_BUMP_FLAG | AUDIO_CAR_EVENT_SCRAPE_FLAG,
						  audio_player_engine_channel);
	assert(impact_feedback_count == 2);

	static void (*const other_sounds[])(legacy_s16) = {
		audio_play_engine_start, audio_play_engine_stop, audio_play_engine_failure,
		audio_play_paved_skid, audio_play_offroad_skid};
	for (legacy_u16 sound = 0; sound < sizeof(other_sounds) / sizeof(other_sounds[0]); sound++) {
		reset_impact_sound();
		other_sounds[sound](audio_player_engine_channel);
		assert(impact_feedback_count == 0);
	}
}
#endif

int main(void)
{
#ifdef RESTUNTS_SDL3
	test_impact_feedback();
#else
	/* The trace covers the DOS pointer layout; SDL3 uses native engine definitions. */
	test_envelopes();
	test_context_selection();
	test_engine_initialization();
	test_driver_timer();
	assert(trace_hash == UINT64_C(0xf07059b72776afde));
#endif
	printf("test-audio-engine: passed\n");
	return 0;
}
