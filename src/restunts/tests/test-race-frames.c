/* Full frame-loop endpoint traces captured before extraction cover waiting,
 * live/replay/paused input, exit requests and sequential drawing decisions. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../c/race.c"
#undef printf
#undef memset

static legacy_u32 trace_hash = UINT32_C(2166136261);
static legacy_u32 scenario, frames, keys;
static legacy_u32 scripted_rewind;
static legacy_u32 presented_frames;

#define TEST_HOTKEY_QUEUE_CAPACITY 64U
#define TEST_HOTKEY_IGNORED_COUNT 48U
#define TEST_HOTKEY_CALLBACK 'p'
#define TEST_HOTKEY_ARRIVAL_NS 1000000U
#define TEST_HOTKEY_CATCHUP_FRAMES 4U
#define TEST_HOTKEY_OPEN_REPLAY 1U
#define TEST_DASHBOARD_RENDER_MODES 32U
#define TEST_DASHBOARD_DIRTY_MODE_SHIFT 4U

static struct {
	legacy_u8 active;
	legacy_u16 read, count, actions, callbacks;
	legacy_s16 queue[TEST_HOTKEY_QUEUE_CAPACITY];
	legacy_s16 last_action;
	legacy_u32 action_renders;
#ifdef RESTUNTS_SDL3
	legacy_u64 available_time, action_time;
	legacy_u32 available_physics, action_physics;
	legacy_u16 exit_request, exit_transitions;
	legacy_u32 exit_physics, exit_renders, analog_updates;
	legacy_u64 catchup_delay;
#endif
} hotkey_script;

static struct {
	legacy_u8 active;
	legacy_u8 target;
	legacy_u16 masks[2];
	legacy_u16 overlays[2];
	legacy_u16 static_draws;
	legacy_u16 updates;
	legacy_u16 diagnostic_draws;
	legacy_u8 mouse_hidden;
	legacy_u16 controls;
	struct RECTANGLE clip;
	struct SHAPE2D shapes[2];
} replay_render;

enum REPLAY_TEST_TARGET {
	REPLAY_TEST_RENDER_TARGET,
	REPLAY_TEST_PAGE_TARGET,
	REPLAY_TEST_SCREEN_TARGET
};

legacy_u8 supersight_enabled;

#ifdef RESTUNTS_SDL3
#define TEST_GHOST_DISTANCE_PER_FRAME 100L
#define TEST_REWIND_TOUCH_TRANSITIONS 2U

static legacy_u8 controller_menu_request, controller_menu_testing;
static legacy_u8 controller_start_request, controller_start_testing;
static legacy_u16 controller_start_polls, controller_start_resets;

legacy_u8 sdl3_controller_take_race_start_request(void)
{
	legacy_u8 requested = controller_start_request;
	controller_start_request = 0;
	controller_start_polls++;
	return requested;
}
static legacy_u16 controller_menu_opens, controller_menu_transitions, controller_analog_reads;

legacy_u8 sdl3_controller_take_menu_request(void)
{
	legacy_u8 request = controller_menu_request;
	controller_menu_request = 0;
	return request;
}

legacy_s16 input_update_controller_camera(void)
{
	return 0;
}

void sdl3_input_set_gameplay_active(legacy_u8 active)
{
	(void)active;
}

void sdl3_input_set_replay_active(legacy_u8 active)
{
	(void)active;
}

static legacy_u8 touch_rewind_active;
static legacy_u32 touch_rewind_transitions;

void sdl3_touch_set_rewind_active(legacy_u8 active)
{
	assert(active != touch_rewind_active);
	assert(game_replay_mode == REPLAY_MODE_LIVE);
	touch_rewind_active = active;
	touch_rewind_transitions++;
}

legacy_s16 camera_track_height_offset;
static legacy_u8 scheduled_mode;
static legacy_u8 scheduled_toggle;
static legacy_u64 scheduled_time;
static legacy_u32 scheduled_physics;
static legacy_u32 scheduled_ghosts;
static legacy_u32 scheduled_snapshots;
static legacy_u8 scheduled_ghost_active;
static legacy_u8 scheduled_ghost_sample_succeeds;
static legacy_u32 scheduled_ghost_sample_calls;
static legacy_u32 scheduled_ghost_sample_frame;
static legacy_u16 scheduled_ghost_sample_rate;
static legacy_u32 scheduled_ghost_sample_lag;
static struct CARSTATE scheduled_ghost;
static struct GHOST_CAMERA_STATE scheduled_ghost_camera;
static legacy_s8 scheduled_input[256];
static struct {
	legacy_u64 time;
	legacy_s32 position;
	legacy_s32 authoritative_position;
	legacy_s16 frame;
} scheduled_renders[128];

static void record_scheduled_frame(const struct GAMESTATE *rendered)
{
	assert(frames < sizeof(scheduled_renders) / sizeof(scheduled_renders[0]));
	scheduled_renders[frames].time = scheduled_time;
	scheduled_renders[frames].position = rendered->playerstate.car_position.lx;
	scheduled_renders[frames].authoritative_position = state.playerstate.car_position.lx;
	scheduled_renders[frames].frame = state.game_frame;
	frames++;
}

legacy_u64 presentation_now(void)
{
	return scheduled_time;
}

void sdl3_platform_delay(legacy_u32 milliseconds)
{
	assert(scheduled_mode != 0);
	scheduled_time += (legacy_u64)milliseconds * 1000000U;
	assert(scheduled_time <= PRESENTATION_SECOND_NS);
}

void sdl3_platform_pump(void)
{
	assert(scheduled_mode != 0);
	legacy_u64 callbacks = scheduled_time * framespersec / PRESENTATION_SECOND_NS;
	if (game_replay_mode == REPLAY_MODE_PLAYBACK) {
		if (replay_playback_speed == REPLAY_PLAYBACK_SLOW) {
			callbacks /= 2;
		} else if (replay_playback_speed == REPLAY_PLAYBACK_FAST) {
			callbacks *= 2;
		}
	}
	elapsed_time2 = (legacy_u16)callbacks;
	if (scheduled_time == PRESENTATION_SECOND_NS) {
		race_exit_request = REPLAY_EXIT_REQUESTED;
	}
}

static legacy_s32 test_render_scale = HIRES_SCALE;
static struct {
	legacy_u8 active, begun, expected_redraw;
	legacy_s32 expected_scale;
	legacy_u16 set_calls, draw_calls, end_calls;
} scale_transition;

static void assert_render_scale_before_drawing(void)
{
	if (scale_transition.active != 0) {
		assert(scale_transition.begun != 0);
		assert(test_render_scale == scale_transition.expected_scale);
		assert(full_redraw_frames_remaining == scale_transition.expected_redraw);
	}
}

legacy_s32 hires_render_scale(void)
{
	return test_render_scale;
}

void hires_set_render_scale(legacy_s32 scale)
{
	if (scale_transition.active != 0) {
		assert(scale_transition.begun == 0);
		assert(scale == scale_transition.expected_scale);
		scale_transition.set_calls++;
	}
	test_render_scale = scale;
}
struct FRAME_ADAPTIVE_STATE frame_adaptive;
legacy_s32 frame_adaptive_render_scale(const struct FRAME_ADAPTIVE_STATE *adaptive)
{
	if (adaptive->quality < FRAME_ADAPTIVE_HALF_RESOLUTION) {
		return HIRES_SCALE;
	}
	return adaptive->quality == FRAME_ADAPTIVE_HALF_RESOLUTION ? FRAME_ADAPTIVE_HALF_SCALE
															   : FRAME_ADAPTIVE_MINIMUM_SCALE;
}

void sdl3_video_begin_track_frame(legacy_u8 adaptive)
{
	if (scale_transition.active != 0) {
		assert(scale_transition.begun == 0);
		assert(adaptive == supersight_enabled);
		scale_transition.begun = 1;
		assert_render_scale_before_drawing();
	}
}

void sdl3_video_end_frame(void)
{
	if (scale_transition.active != 0) {
		assert(scale_transition.begun != 0 && scale_transition.draw_calls == 1);
		scale_transition.begun = 0;
		scale_transition.end_calls++;
	}
}

struct CARSTATE *ghost_car_state(void)
{
	return scheduled_ghost_active != 0 ? &scheduled_ghost : NULL;
}

const struct GHOST_CAMERA_STATE *ghost_camera_state(void)
{
	return scheduled_ghost_active != 0 ? &scheduled_ghost_camera : NULL;
}

legacy_s16 ghost_sample_render_pose(legacy_u32 live_frame, legacy_u16 live_frame_rate,
									legacy_u32 lag, struct CARSTATE *car,
									struct GHOST_CAMERA_STATE *camera)
{
	scheduled_ghost_sample_calls++;
	scheduled_ghost_sample_frame = live_frame;
	scheduled_ghost_sample_rate = live_frame_rate;
	scheduled_ghost_sample_lag = lag;
	if (scheduled_ghost_sample_succeeds == 0) {
		return 0;
	}
	*car = scheduled_ghost;
	*camera = scheduled_ghost_camera;
	legacy_u64 live_time = (legacy_u64)live_frame * FRAME_INTERPOLATION_ONE;
	live_time = live_time > lag ? live_time - lag : 0;
	car->car_position.lx =
		(legacy_s32)(live_time * TEST_GHOST_DISTANCE_PER_FRAME / FRAME_INTERPOLATION_ONE);
	camera->follow_position.x = (legacy_s16)car->car_position.lx;
	return 1;
}

void update_frame_snapshot(legacy_s8 buffer, struct RECTANGLE *rect,
						   const struct GAMESTATE *snapshot, const struct CARSTATE *ghost,
						   const struct GHOST_CAMERA_STATE *ghost_camera)
{
	(void)buffer;
	(void)rect;
	assert(scheduled_mode != 0 && supersight_enabled != 0);
	if (scheduled_ghost_active != 0) {
		assert(ghost == &race_presentation.ghost_interpolated);
		assert(ghost_camera == &race_presentation.ghost_camera_interpolated);
	} else {
		assert(ghost == NULL && ghost_camera == NULL);
	}
	assert(snapshot->game_frame == state.game_frame);
	assert(snapshot->playerstate.car_position.lx <= state.playerstate.car_position.lx);
	if (race_presentation.history_valid != 0) {
		assert(snapshot->playerstate.car_position.lx >=
			   race_presentation.previous.playerstate.car_position.lx);
	}
	scheduled_snapshots++;
	record_scheduled_frame(snapshot);
}
#endif

void frame_supersight_reset(void)
{
}

void frame_fps_reset(void)
{
	presented_frames = 0;
}

void frame_fps_present_roof(void)
{
}

legacy_s16 frame_display_overlay_active(void)
{
	return replay_render.active;
}

struct RECTANGLE *frame_fps_draw_text(void)
{
	if (replay_render.active != 0) {
		/* Custom top artwork and instruments must be composed before the text. */
		assert(replay_render.masks[0] + replay_render.masks[1] == frames);
		assert(replay_render.overlays[0] + replay_render.overlays[1] == frames);
		assert(replay_render.updates == frames);
		assert(replay_render.mouse_hidden != 0);
		assert(replay_render.target == (video_uses_page_flipping != 0 ? REPLAY_TEST_PAGE_TARGET
																	  : REPLAY_TEST_SCREEN_TARGET));
		replay_render.diagnostic_draws++;
		assert(replay_render.diagnostic_draws == frames);
	}
	return &empty_rect;
}

void frame_fps_record_presented(void)
{
	presented_frames++;
	assert(presented_frames == frames);
}
void ghost_update(legacy_u32 frame, legacy_u16 frame_rate)
{
#ifdef RESTUNTS_SDL3
	if (scheduled_mode != 0) {
		scheduled_ghosts++;
		if (scheduled_ghost_active != 0) {
			scheduled_ghost.car_position.lx = frame * TEST_GHOST_DISTANCE_PER_FRAME;
			scheduled_ghost_camera.follow_position.x = (legacy_s16)scheduled_ghost.car_position.lx;
		}
	}
#endif
	assert(frame == (game_replay_mode == REPLAY_MODE_PAUSED
						 ? 0
						 : (legacy_u32)(legacy_u16)state.game_frame + elapsed_time1));
	assert(frame_rate == (legacy_u16)framespersec);
}
static struct RECTANGLE dirty_rect;
static legacy_s8 text_resource[8];
static void trace(legacy_u32 value)
{
	if (replay_render.active != 0) {
		return;
	}
	for (legacy_u32 i = 0; i < 4; i++) {
		trace_hash = (trace_hash ^ (value & 255U)) * UINT32_C(16777619);
		value >>= 8;
	}
}
static void trace_rect(const struct RECTANGLE *rect)
{
	trace(rect->left);
	trace(rect->right);
	trace(rect->top);
	trace(rect->bottom);
}
/* Q-up polling and rewind-only dependencies must not alter the legacy trace. */
legacy_s16 kb_get_key_state(legacy_s16 scan_code)
{
#ifdef RESTUNTS_SDL3
	if (hotkey_script.catchup_delay != 0) {
		sdl3_platform_pump();
	}
#endif
	if (scan_code == RACE_CONTROL_SCAN_CODE) {
		return 0;
	}
	assert(scan_code == RACE_REWIND_SCAN_CODE);
	return scripted_rewind != 0 && frames < 2;
}
void dos_interrupts_disable(void)
{
	assert(scripted_rewind != 0);
}
void dos_interrupts_enable(void)
{
	assert(scripted_rewind != 0);
}
legacy_u32 timer_get_delta_alt(void)
{
	assert(scripted_rewind != 0);
	return 20;
}
void restore_gamestate(legacy_u16 target)
{
	assert(scripted_rewind != 0);
	state.game_frame = target;
	elapsed_time2 = target;
}
legacy_u8 dos_joystick_is_enabled(void)
{
	trace(1);
	return scenario & 1;
}
void replay_apply_analog_steering_history(void)
{
#ifdef RESTUNTS_SDL3
	if (controller_menu_testing != 0) {
		assert(controller_analog_reads == scheduled_physics);
		controller_analog_reads++;
		scheduled_input[state.game_frame] |= INPUT_STEER_RIGHT_FLAG;
	}
	if (hotkey_script.active != 0) {
		hotkey_script.analog_updates++;
	}
#endif
	trace(2);
}
void update_gamestate(void)
{
#ifdef RESTUNTS_SDL3
	if (scheduled_mode != 0) {
		scheduled_physics++;
		if (hotkey_script.catchup_delay != 0 && hotkey_script.actions == 0) {
			/* Rendering never becomes ready when each update takes a full input interval. */
			scheduled_time += hotkey_script.catchup_delay;
			assert(scheduled_physics < TEST_HOTKEY_CATCHUP_FRAMES);
		}
		state.playerstate.car_position.lx += 60;
	}
#endif
	trace(3);
	state.game_frame++;
}
void init_rect_arrays(void)
{
	trace(4);
}
void input_push_status(void)
{
	trace(5);
}
void input_pop_status(void)
{
	trace(6);
}
void audio_suspend(void)
{
	trace(7);
}
void audio_resume(void)
{
	trace(8);
}
legacy_s8 far *locate_text_res(legacy_s8 far *resource, const legacy_s8 *name)
{
	(void)resource;
	trace(9);
	trace(name[0]);
	return text_resource;
}
legacy_u16 show_dialog(legacy_s16 type, legacy_s16 save, void far *text, legacy_u16 x, legacy_u16 y,
					   legacy_s16 color, legacy_s16 *flags, legacy_s16 selected)
{
	assert(text == text_resource);
	assert(flags == 0);
	trace(10);
	trace(type);
	trace(save);
	trace(x);
	trace(y);
	trace(color);
	trace(selected);
	return scenario & 1 ? 65535U : scenario & 2;
}
void dos_timer_set_callbacks_suspended(legacy_s16 value)
{
	trace(11);
	trace(value);
}
void update_crash_state(legacy_s16 event, legacy_s16 car)
{
	trace(12);
	trace(event);
	trace(car);
}
void sprite_select_mcga_backbuffer(void)
{
	replay_render.target = REPLAY_TEST_PAGE_TARGET;
	trace(13);
}
void sprite_select_render_window(void)
{
	replay_render.target = REPLAY_TEST_RENDER_TARGET;
	trace(14);
}
void set_projection(legacy_s16 x, legacy_s16 y, legacy_s16 width, legacy_s16 height)
{
	trace(15);
	trace(x);
	trace(y);
	trace(width);
	trace(height);
}
void sprite_set_target_clip_bounds(legacy_u16 left, legacy_u16 right, legacy_u16 top,
								   legacy_u16 bottom)
{
	replay_render.clip = (struct RECTANGLE){left, right, top, bottom};
	trace(16);
	trace(left);
	trace(right);
	trace(top);
	trace(bottom);
}
void setup_car_shapes(legacy_s16 operation)
{
#ifdef RESTUNTS_SDL3
	assert_render_scale_before_drawing();
#endif
	if (replay_render.active != 0) {
		if (operation == DASHBOARD_OPERATION_REDRAW_STATIC) {
			replay_render.static_draws++;
		} else {
			assert(operation == DASHBOARD_OPERATION_UPDATE);
			assert(replay_render.target == (video_uses_page_flipping != 0
												? REPLAY_TEST_PAGE_TARGET
												: REPLAY_TEST_SCREEN_TARGET));
			replay_render.updates++;
		}
	}
	trace(17);
	trace(operation);
}
void loop_game(legacy_s16 operation, legacy_s16 recorded, legacy_s16 current)
{
#ifdef RESTUNTS_SDL3
	if (controller_menu_testing != 0) {
		assert(game_replay_mode == REPLAY_MODE_PLAYBACK);
		if (operation == REPLAY_LOOP_SELECT_CONTROL) {
			assert(recorded == REPLAY_CONTROL_PAUSE);
			controller_menu_transitions++;
		} else if (operation == REPLAY_LOOP_OPEN_MENU) {
			controller_menu_opens++;
			race_exit_request = REPLAY_EXIT_REQUESTED;
		} else {
			assert(operation == REPLAY_LOOP_LOAD_RESOURCES);
		}
		return;
	}
#endif
	if (replay_render.active != 0) {
		assert(operation == REPLAY_LOOP_DRAW_CONTROLS);
		replay_render.controls++;
		return;
	}
#ifdef RESTUNTS_SDL3
	if (scheduled_mode != 0) {
		if (hotkey_script.active != 0 && hotkey_script.exit_request == TEST_HOTKEY_OPEN_REPLAY &&
			operation == REPLAY_LOOP_SELECT_CONTROL) {
			assert(recorded == REPLAY_CONTROL_PAUSE);
			assert(game_replay_mode == REPLAY_MODE_PLAYBACK);
			hotkey_script.exit_transitions++;
			hotkey_script.exit_physics = scheduled_physics;
			hotkey_script.exit_renders = frames;
			/* End the fixture after observing the request to open replay controls. */
			race_exit_request = REPLAY_EXIT_REQUESTED;
		}
		if (operation == REPLAY_LOOP_HANDLE_INPUT) {
			keys++;
		}
		return;
	}
#endif
	assert(scripted_rewind == 0);
	trace(18);
	trace(operation);
	trace(recorded);
	trace(current);
}
void update_frame(legacy_s8 buffer, struct RECTANGLE *rect)
{
#ifdef RESTUNTS_SDL3
	assert_render_scale_before_drawing();
	if (scale_transition.active != 0) {
		scale_transition.draw_calls++;
	}
#endif
	if (replay_render.active != 0) {
		assert(rect == &rect_windshield);
		assert(rect->bottom == (supersight_enabled != 0 && replaybar_enabled != 0 ? 151 : 165));
		frames++;
		return;
	}
#ifdef RESTUNTS_SDL3
	if (scheduled_mode != 0) {
		record_scheduled_frame(&state);
		return;
	}
#endif
	trace(19);
	trace(buffer);
	trace_rect(rect);
	trace(state.game_frame);
	trace(gameconfig.game_recordedframes);
	assert(++frames <= 3);
	if (scripted_rewind != 0) {
		assert(replaybar_enabled == 0);
		assert(keys == 0);
		if (frames == 3) {
			assert(game_replay_mode == REPLAY_MODE_LIVE);
			race_exit_request = REPLAY_EXIT_REQUESTED;
		} else {
			assert(game_replay_mode == REPLAY_MODE_PLAYBACK);
		}
		return;
	}
	elapsed_time2 = ++state.game_frame;
	if (frames == 3) {
		race_exit_request = REPLAY_EXIT_REQUESTED;
	}
}
void rect_union(struct RECTANGLE *first, struct RECTANGLE *second, struct RECTANGLE *result)
{
	assert(first == result);
	trace(20);
	trace_rect(first);
	trace_rect(second);
}
static void record_dashboard_mask(legacy_u8 clipped, legacy_u8 overlay)
{
	if (replay_render.active == 0) {
		return;
	}
	assert(replay_render.target ==
		   (video_uses_page_flipping != 0 ? REPLAY_TEST_PAGE_TARGET : REPLAY_TEST_RENDER_TARGET));
	assert(clipped == (supersight_enabled != 0 && replaybar_enabled != 0));
	if (clipped != 0) {
		assert(replay_render.clip.left == 0 && replay_render.clip.right == 320);
		assert(replay_render.clip.top == 0 && replay_render.clip.bottom == 151);
	}
	if (overlay != 0) {
		replay_render.overlays[clipped]++;
	} else {
		replay_render.masks[clipped]++;
	}
}

void shape2d_render_bmp_as_mask(struct SHAPE2D far *shape)
{
	record_dashboard_mask(0, 0);
	(void)shape;
	trace(21);
}
void shape2d_rle_or_far_pointer(legacy_u16 offset, legacy_u16 segment)
{
	record_dashboard_mask(0, 1);
	trace(22);
	trace(offset);
	trace(segment);
}
void shape2d_rle_mask_position_clipped(struct SHAPE2D far *shape)
{
	record_dashboard_mask(1, 0);
	assert(shape == dasmshapeptr);
	assert(supersight_enabled != 0 && replaybar_enabled != 0);
}
void shape2d_rle_or_position_clipped(struct SHAPE2D far *shape)
{
	record_dashboard_mask(1, 1);
	assert(shape == (replay_render.active != 0 ? &replay_render.shapes[1] : NULL));
	assert(supersight_enabled != 0 && replaybar_enabled != 0);
}
void far *dos_memory_make_pointer(legacy_u16 segment, legacy_u16 offset)
{
	assert(segment == dastseg && offset == dastbmp_y2);
	return replay_render.active != 0 ? &replay_render.shapes[1] : NULL;
}
void frame_present(struct RECTANGLE *rect)
{
	if (replay_render.active != 0 && video_uses_page_flipping == 0) {
		replay_render.target = REPLAY_TEST_SCREEN_TARGET;
	}
	trace(23);
	trace_rect(rect);
}
void mouse_draw_opaque_check(void)
{
	replay_render.mouse_hidden = 1;
	trace(24);
}
void mouse_draw_transparent_check(void)
{
	replay_render.mouse_hidden = 0;
	trace(25);
}
void sprite_present_mcga_backbuffer(void)
{
	trace(26);
}
void init_game_state_with_frame_rate(legacy_u16 rate)
{
#ifdef RESTUNTS_SDL3
	if (controller_start_testing != 0) {
		assert(rate == configured_frame_rate);
		controller_start_resets++;
	}
#endif
	trace(27);
	trace(rate);
}
void mouse_minmax_position(legacy_s16 inset)
{
	trace(28);
	trace(inset);
}
void audio_carstate(void)
{
	trace(29);
}
static legacy_s16 hotkey_script_pending(void)
{
	if (hotkey_script.active == 0 || hotkey_script.read == hotkey_script.count) {
		return 0;
	}
#ifdef RESTUNTS_SDL3
	if (scheduled_mode != 0 && (scheduled_time < hotkey_script.available_time ||
								scheduled_physics < hotkey_script.available_physics)) {
		return 0;
	}
#endif
	return hotkey_script.queue[hotkey_script.read];
}

legacy_s16 kb_checking(void)
{
	/* Existing cadence and replay traces have no asynchronous keyboard arrivals. */
	return hotkey_script_pending();
}

legacy_s16 dos_kb_get_char(void)
{
	if (hotkey_script.active != 0) {
		keys++;
		legacy_s16 key = hotkey_script_pending();
		if (key != 0) {
			hotkey_script.read++;
			if (key == TEST_HOTKEY_CALLBACK) {
				/* Registered callbacks consume their key before returning to the race. */
				hotkey_script.callbacks++;
				return 0;
			}
		}
		return key;
	}
#ifdef RESTUNTS_SDL3
	if (scheduled_mode != 0) {
		keys++;
		if (scheduled_toggle != 0 && state.game_frame == framespersec / 2) {
			supersight_enabled = 1;
		}
		return 0;
	}
#endif
	trace(30);
	keys++;
	if (idle_expired) {
		return scenario & 1 ? 27 : 0;
	}
	return keys % 3 == 1 ? KEY_LEFT : 0;
}
legacy_s16 handle_ingame_kb_shortcuts(legacy_s16 key)
{
#ifdef RESTUNTS_SDL3
	if (controller_menu_testing != 0) {
		assert(key == KEY_ESCAPE);
		state.game_end_event = CRASH_EVENT_EXIT;
		race_exit_request = 1;
		return 1;
	}
#endif
	if (hotkey_script.active != 0) {
		if (key != KEY_F11 && key != KEY_F12 && key != KEY_ESCAPE && key != 'd') {
			return 0;
		}
		hotkey_script.actions++;
		hotkey_script.last_action = key;
		hotkey_script.action_renders = frames;
#ifdef RESTUNTS_SDL3
		hotkey_script.action_time = scheduled_time;
		hotkey_script.action_physics = scheduled_physics;
		if (hotkey_script.exit_request != 0) {
			assert(key == KEY_ESCAPE);
			/* Match Escape's exit event, or a callback requesting the main menu. */
			state.game_end_event = CRASH_EVENT_EXIT;
			race_exit_request = hotkey_script.exit_request;
		}
#endif
		return 1;
	}
	trace(31);
	trace(key);
	return 0;
}
void dos_mouse_get_state(legacy_s16 *buttons, legacy_s16 *x, legacy_s16 *y)
{
	trace(32);
	*buttons = scenario & 1;
	*x = 90;
	*y = 150;
}
legacy_s16 get_kb_or_joy_flags(void)
{
	trace(33);
	return scenario & 2 ? INPUT_ACTION_BUTTON_MASK : 0;
}

static void test_driving_hotkey_queue(void)
{
	static const legacy_s16 ignored[] = {KEY_HOME, KEY_END, KEY_SPACE, KEY_ENTER, 'a', 'z', 'q'};
	static const legacy_s16 shortcuts[] = {KEY_F11, KEY_F12, KEY_ESCAPE, 'd', TEST_HOTKEY_CALLBACK};
	game_replay_mode = REPLAY_MODE_LIVE;
	for (legacy_u16 shortcut = 0; shortcut < sizeof(shortcuts) / sizeof(shortcuts[0]); shortcut++) {
		memset(&hotkey_script, 0, sizeof(hotkey_script));
		hotkey_script.active = 1;
		for (legacy_u16 index = 0; index < TEST_HOTKEY_IGNORED_COUNT; index++) {
			hotkey_script.queue[hotkey_script.count++] =
				ignored[index % (sizeof(ignored) / sizeof(ignored[0]))];
		}
		hotkey_script.queue[hotkey_script.count++] = shortcuts[shortcut];
		hotkey_script.queue[hotkey_script.count++] = KEY_F12;
		race_handle_driving_input();
		/* One poll passes every ignored driving control and reaches the command. */
		assert(hotkey_script.read == TEST_HOTKEY_IGNORED_COUNT + 1U);
		legacy_u16 callback = shortcuts[shortcut] == TEST_HOTKEY_CALLBACK;
		assert(hotkey_script.callbacks == callback);
		assert(hotkey_script.actions == 1U - callback);
		if (callback == 0) {
			assert(hotkey_script.last_action == shortcuts[shortcut]);
		}
		/* Keep the following command queued across actions and modal callbacks. */
		assert(kb_checking() == KEY_F12);
		race_handle_driving_input();
		assert(hotkey_script.read == hotkey_script.count);
		assert(hotkey_script.actions == 2U - callback);
		assert(hotkey_script.last_action == KEY_F12);
		assert(kb_checking() == 0);
	}
	memset(&hotkey_script, 0, sizeof(hotkey_script));
}

static void test_replay_dashboard_rendering(void)
{
	legacy_u32 previous_trace = trace_hash;
	for (legacy_u16 mode = 0; mode < TEST_DASHBOARD_RENDER_MODES; mode++) {
		memset(&replay_render, 0, sizeof(replay_render));
		replay_render.active = 1;
		supersight_enabled = mode & 1;
		replaybar_toggle = (mode >> 1) & 1;
		video_uses_page_flipping = (mode >> 2) & 1;
		legacy_u8 full_redraw = (mode >> 3) & 1;
		struct RACE_VIEWPORT_CACHE cache = {-1, -1, 0};
		game_replay_mode = REPLAY_MODE_PLAYBACK;
		game_replay_mode_copy = RACE_REPLAY_MODE_UNINITIALIZED;
		idle_expired = followOpponentFlag = is_in_replay = 0;
		dashb_toggle = 1;
		dashbmp_y = 165;
		dastbmp_y = 130;
		roofbmpheight = 0;
		dasmshapeptr = &replay_render.shapes[0];
		viewport_bottom_cache = -1;
		video_page_count = video_uses_page_flipping != 0 ? 2 : 1;
		frame_buffer_index = dashboard_buffer_index = 0;
		slow_video_mgmt_copy = (mode >> TEST_DASHBOARD_DIRTY_MODE_SHIFT) & 1U;
		frames = presented_frames = 0;
		race_update_viewport(&cache, 0);
		legacy_u8 clipped = supersight_enabled != 0 && replaybar_enabled != 0;
		assert(rect_windshield.bottom == (clipped != 0 ? 151 : 165));
		assert(dashboard_visible != 0);
		full_redraw_frames_remaining = full_redraw;
		if (video_uses_page_flipping != 0) {
			sprite_select_mcga_backbuffer();
		} else {
			sprite_select_render_window();
		}
		race_draw_frame();
		assert(replay_render.masks[clipped] == 1 && replay_render.overlays[clipped] == 1);
		assert(replay_render.masks[!clipped] == 0 && replay_render.overlays[!clipped] == 0);
		assert(replay_render.static_draws == full_redraw && replay_render.updates == 1);
		assert(replay_render.diagnostic_draws == 1);
		assert(replay_render.controls == (full_redraw != 0 && replaybar_enabled != 0));
		assert(replay_render.clip.left == 0 && replay_render.clip.right == 320);
		assert(replay_render.clip.top == 0 && replay_render.clip.bottom == 200);
		assert(frame_buffer_index == (video_uses_page_flipping != 0 ? 1 : 0));
		assert(full_redraw_frames_remaining == 0 && presented_frames == 1);
	}
	replay_render.active = 0;
	supersight_enabled = 0;
	assert(trace_hash == previous_trace);
}

static void test_rewind_frame_loop(void)
{
	struct RACE_VIEWPORT_CACHE cache = {-1, -1, 0};
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	scripted_rewind = 1;
#ifdef RESTUNTS_SDL3
	touch_rewind_transitions = 0;
#endif
	frames = keys = 0;
	state.game_frame = elapsed_time2 = 10;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.game_frames_per_sec = 40;
	gameconfig.game_recordedframes = 10;
	game_replay_mode = REPLAY_MODE_LIVE;
	game_replay_mode_copy = -1;
	race_exit_request = 0;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	idle_expired = video_uses_page_flipping = recording_limit_warning_requested = 0;
	slow_video_mgmt_copy = slow_video_mgmt = 0;
	frame_buffer_index = dashboard_buffer_index = 0;
	dashb_toggle = is_in_replay = followOpponentFlag = 0;
	replaybar_toggle = 1;
	full_redraw_frames_remaining = 1;
	height_above_replaybar = 200;
	viewport_bottom_cache = -1;
	timer_ticks_per_frame = 5;
	race_run_frames(&cache);
	assert(frames == 3 && keys == 0);
	assert(state.game_frame == 7 && elapsed_time2 == 7);
	assert(gameconfig.game_recordedframes == 7);
	assert(game_replay_mode == REPLAY_MODE_LIVE && is_in_replay == 0);
#ifdef RESTUNTS_SDL3
	assert(!touch_rewind_active && touch_rewind_transitions == TEST_REWIND_TOUCH_TRANSITIONS);
#endif
}

#ifdef RESTUNTS_SDL3
static void test_render_scale_transitions(void)
{
	static const struct {
		legacy_u16 quality;
		legacy_u8 supersight;
		legacy_s32 scale;
	} stages[] = {{FRAME_ADAPTIVE_FULL_VIEW, 1, HIRES_SCALE},
				  {FRAME_ADAPTIVE_LARGE_VIEW, 1, HIRES_SCALE},
				  {FRAME_ADAPTIVE_HALF_RESOLUTION, 1, FRAME_ADAPTIVE_HALF_SCALE},
				  {FRAME_ADAPTIVE_MINIMUM_RESOLUTION, 1, FRAME_ADAPTIVE_MINIMUM_SCALE},
				  {FRAME_ADAPTIVE_SMALL_VIEW, 1, FRAME_ADAPTIVE_MINIMUM_SCALE},
				  /* F12 disables SuperSight even if its previous stage used a small raster. */
				  {FRAME_ADAPTIVE_SMALL_VIEW, 0, HIRES_SCALE},
				  {FRAME_ADAPTIVE_MINIMUM_RESOLUTION, 1, FRAME_ADAPTIVE_MINIMUM_SCALE},
				  {FRAME_ADAPTIVE_FULL_VIEW, 1, HIRES_SCALE}};
	const legacy_u32 previous_trace = trace_hash;
	scheduled_mode = 0;
	race_presentation.interpolating = 0;
	for (legacy_u8 pages = 1; pages <= 2; pages++) {
		test_render_scale = HIRES_SCALE;
		for (size_t stage = 0; stage < sizeof(stages) / sizeof(stages[0]); stage++) {
			memset(&replay_render, 0, sizeof(replay_render));
			replay_render.active = 1;
			supersight_enabled = stages[stage].supersight;
			frame_adaptive.quality = stages[stage].quality;
			legacy_u8 changed = test_render_scale != stages[stage].scale;
			struct RACE_VIEWPORT_CACHE cache = {-1, -1, 0};
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			game_replay_mode_copy = RACE_REPLAY_MODE_UNINITIALIZED;
			idle_expired = followOpponentFlag = is_in_replay = 0;
			dashb_toggle = replaybar_toggle = 1;
			dashbmp_y = 165;
			dastbmp_y = 130;
			roofbmpheight = 0;
			dasmshapeptr = &replay_render.shapes[0];
			viewport_bottom_cache = -1;
			video_page_count = pages;
			video_uses_page_flipping = pages > 1;
			frame_buffer_index = dashboard_buffer_index = 0;
			slow_video_mgmt_copy = 0;
			frames = presented_frames = 0;
			race_update_viewport(&cache, 0);
			/* Isolate the scale-triggered invalidation from viewport changes. */
			full_redraw_frames_remaining = 0;
			memset(&scale_transition, 0, sizeof(scale_transition));
			scale_transition.active = 1;
			scale_transition.expected_scale = stages[stage].scale;
			for (legacy_u8 draw = 0; draw <= pages; draw++) {
				scale_transition.expected_redraw = changed != 0 && draw < pages ? pages - draw : 0;
				scale_transition.draw_calls = 0;
				if (video_uses_page_flipping != 0) {
					sprite_select_mcga_backbuffer();
				} else {
					sprite_select_render_window();
				}
				race_draw_frame();
				assert(test_render_scale == stages[stage].scale);
				assert(scale_transition.set_calls == changed);
				assert(scale_transition.begun == 0 && scale_transition.draw_calls == 1);
				assert(scale_transition.end_calls == draw + 1U);
				assert(full_redraw_frames_remaining == (scale_transition.expected_redraw != 0
															? scale_transition.expected_redraw - 1
															: 0));
			}
			assert(replay_render.static_draws == (changed != 0 ? pages : 0));
			assert(replay_render.controls == (changed != 0 ? pages : 0));
			assert(replay_render.updates == pages + 1U);
			scale_transition.active = 0;
		}
	}
	replay_render.active = 0;
	supersight_enabled = 0;
	frame_adaptive.quality = FRAME_ADAPTIVE_FULL_VIEW;
	test_render_scale = HIRES_SCALE;
	assert(trace_hash == previous_trace);
}

static void prepare_presentation_test(legacy_u16 rate, legacy_u16 mode)
{
	memset(&state, 0, sizeof(state));
	memset(&gameconfig, 0, sizeof(gameconfig));
	scheduled_mode = 1;
	scheduled_toggle = mode == 2;
	scheduled_time = 0;
	scheduled_physics = scheduled_ghosts = scheduled_snapshots = 0;
	scheduled_ghost_active = 0;
	scheduled_ghost_sample_succeeds = 1;
	scheduled_ghost_sample_calls = 0;
	memset(scheduled_renders, 0, sizeof(scheduled_renders));
	scripted_rewind = 0;
	supersight_enabled = mode == 1;
	frames = keys = presented_frames = 0;
	state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
	state.game_frames_per_sec = framespersec = configured_frame_rate = rate;
	game_replay_mode = REPLAY_MODE_LIVE;
	game_replay_mode_copy = -1;
	replay_playback_speed = REPLAY_PLAYBACK_NORMAL;
	race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
	elapsed_time1 = elapsed_time2 = race_exit_request = 0;
	idle_expired = video_uses_page_flipping = recording_limit_warning_requested = 0;
	slow_video_mgmt = slow_video_mgmt_copy = 0;
	mouse_driving_enabled = 0;
	frame_buffer_index = dashboard_buffer_index = 0;
	dashb_toggle = is_in_replay = followOpponentFlag = replaybar_toggle = 0;
	full_redraw_frames_remaining = 1;
	height_above_replaybar = 200;
	viewport_bottom_cache = -1;
}

enum TEST_HOTKEY_TIMING {
	TEST_HOTKEY_BETWEEN_SAMPLES,
	TEST_HOTKEY_DURING_CATCHUP,
	TEST_HOTKEY_DURING_STALL,
	TEST_HOTKEY_TIMING_COUNT
};

static void test_controller_start_from_truck(void)
{
	static const legacy_s8 sequences[] = {RACE_START_SEQUENCE_FLAG_ANIMATION,
										  RACE_START_SEQUENCE_AUTO_DRIVE,
										  RACE_START_SEQUENCE_INACTIVE};
	for (legacy_u32 index = 0; index < sizeof(sequences) / sizeof(sequences[0]); index++) {
		prepare_presentation_test(GAME_FRAME_RATE_NORMAL, 1);
		game_replay_mode = REPLAY_MODE_PAUSED;
		state.game_inputmode = GAME_INPUT_MODE_WAITING;
		race_start_sequence_state = sequences[index];
		controller_start_testing = 1;
		controller_start_request = 0;
		controller_start_polls = controller_start_resets = 0;
		scenario = 0;
		race_handle_driving_input();
		assert(game_replay_mode == REPLAY_MODE_PAUSED);
		assert(race_start_sequence_state == sequences[index]);
		assert(controller_start_resets == 0 && controller_start_polls == 1);

		/* The backend retains a complete stick flick even if it is centered by
		 * this poll. Start once through the same reset as keyboard arrows. */
		controller_start_request = 1;
		race_handle_driving_input();
		assert(game_replay_mode == REPLAY_MODE_LIVE);
		assert(race_start_sequence_state == RACE_START_SEQUENCE_INACTIVE);
		assert(controller_start_request == 0 && controller_start_resets == 1);
		race_handle_driving_input();
		assert(controller_start_resets == 1 && controller_start_polls == 2);
		controller_start_testing = 0;
	}
	prepare_presentation_test(GAME_FRAME_RATE_NORMAL, 1);
	game_replay_mode = REPLAY_MODE_PLAYBACK;
	controller_start_request = 1;
	controller_start_polls = 0;
	race_handle_driving_input();
	assert(controller_start_polls == 0 && controller_start_request != 0);
	controller_start_request = 0;
}

static void test_controller_menu_transition(void)
{
	static const legacy_s8 modes[] = {REPLAY_MODE_LIVE, REPLAY_MODE_PAUSED, REPLAY_MODE_PLAYBACK};
	for (legacy_u32 index = 0; index < sizeof(modes) / sizeof(modes[0]); index++) {
		prepare_presentation_test(GAME_FRAME_RATE_NORMAL, 1);
		game_replay_mode = modes[index];
		controller_menu_request = controller_menu_testing = 1;
		controller_menu_opens = controller_menu_transitions = controller_analog_reads = 0;
		struct RACE_REWIND_STATE rewind = {0};
		legacy_s16 last_frame = -1;
		legacy_u8 menu_pending = 0;
		if (modes[index] == REPLAY_MODE_LIVE) {
			elapsed_time2 = TEST_HOTKEY_CATCHUP_FRAMES;
			assert(race_handle_controller_menu(&rewind, &last_frame, &menu_pending) == 0);
			assert(controller_menu_request == 0 && menu_pending != 0);
			for (legacy_u16 frame = 0; frame < TEST_HOTKEY_CATCHUP_FRAMES; frame++) {
				assert(race_frame_is_ready(&last_frame) == 0);
				assert(controller_menu_opens == 0);
				assert((scheduled_input[frame] & INPUT_STEER_RIGHT_FLAG) != 0);
			}
			assert(controller_analog_reads == TEST_HOTKEY_CATCHUP_FRAMES);
		}
		assert(race_handle_controller_menu(&rewind, &last_frame, &menu_pending) != 0);
		assert(controller_menu_opens == 1);
		assert(controller_menu_transitions == (modes[index] != REPLAY_MODE_PLAYBACK));
		assert(scheduled_physics ==
			   (modes[index] == REPLAY_MODE_LIVE ? TEST_HOTKEY_CATCHUP_FRAMES : 0));
		assert(controller_menu_request == 0 && menu_pending == 0);
		assert(race_handle_controller_menu(&rewind, &last_frame, &menu_pending) == 0);
		assert(controller_menu_opens == 1);
		controller_menu_testing = 0;
	}
}

static void test_hotkeys_during_frame_waits(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_NORMAL};
	for (legacy_u16 rate_index = 0; rate_index < sizeof(rates) / sizeof(rates[0]); rate_index++) {
		for (legacy_u16 mode = 0; mode < 2; mode++) {
			for (legacy_u16 timing = TEST_HOTKEY_BETWEEN_SAMPLES; timing < TEST_HOTKEY_TIMING_COUNT;
				 timing++) {
				struct RACE_VIEWPORT_CACHE cache = {-1, -1, 0};
				legacy_u16 rate = rates[rate_index];
				prepare_presentation_test(rate, mode);
				memset(&hotkey_script, 0, sizeof(hotkey_script));
				hotkey_script.active = 1;
				hotkey_script.queue[hotkey_script.count++] = KEY_F11;
				if (timing != TEST_HOTKEY_BETWEEN_SAMPLES) {
					/* A key arrives during catch-up, with three physics updates outstanding. */
					scheduled_time = TEST_HOTKEY_CATCHUP_FRAMES * PRESENTATION_SECOND_NS / rate;
					elapsed_time2 = TEST_HOTKEY_CATCHUP_FRAMES;
					hotkey_script.available_physics = 1;
					if (timing == TEST_HOTKEY_DURING_STALL) {
						hotkey_script.catchup_delay = PRESENTATION_SECOND_NS / rate;
					}
				} else {
					/* A key arrives just after presentation, before the next input sample. */
					hotkey_script.available_time = TEST_HOTKEY_ARRIVAL_NS;
				}
				legacy_u64 expected_time =
					scheduled_time + hotkey_script.available_time + hotkey_script.catchup_delay;
				race_run_frames(&cache);
				assert(hotkey_script.actions == 1 && hotkey_script.last_action == KEY_F11);
				assert(hotkey_script.action_time == expected_time);
				assert(hotkey_script.action_physics ==
					   (timing != TEST_HOTKEY_BETWEEN_SAMPLES ? 1U : 0U));
				assert(hotkey_script.action_renders ==
					   (timing != TEST_HOTKEY_BETWEEN_SAMPLES ? 0U : 1U));
				/* Polling commands does not change the authoritative simulation schedule. */
				assert(scheduled_physics == rate && (legacy_u16)state.game_frame == rate);
				memset(&hotkey_script, 0, sizeof(hotkey_script));
			}
		}
	}
}

static void test_hotkey_exit_during_catchup(void)
{
	for (legacy_u16 request = TEST_HOTKEY_OPEN_REPLAY; request <= REPLAY_EXIT_REQUESTED;
		 request++) {
		struct RACE_VIEWPORT_CACHE cache = {-1, -1, 0};
		prepare_presentation_test(GAME_FRAME_RATE_NORMAL, 1);
		scheduled_time = TEST_HOTKEY_CATCHUP_FRAMES * PRESENTATION_SECOND_NS / framespersec;
		elapsed_time2 = TEST_HOTKEY_CATCHUP_FRAMES;
		memset(&hotkey_script, 0, sizeof(hotkey_script));
		hotkey_script.active = 1;
		hotkey_script.queue[hotkey_script.count++] = KEY_ESCAPE;
		hotkey_script.available_physics = 1;
		hotkey_script.exit_request = request;
		mouse_driving_enabled = 1;
		race_run_frames(&cache);
		assert(hotkey_script.actions == 1 && hotkey_script.action_physics == 1);
		assert(hotkey_script.action_renders == 0);
		/* Queued live samples must retain their analog steering before replay opens. */
		assert(hotkey_script.analog_updates == TEST_HOTKEY_CATCHUP_FRAMES);
		assert(scheduled_physics == TEST_HOTKEY_CATCHUP_FRAMES);
		if (request == REPLAY_EXIT_REQUESTED) {
			assert(hotkey_script.exit_transitions == 0);
			assert(frames == 1);
		} else {
			assert(hotkey_script.exit_transitions == 1);
			assert(hotkey_script.exit_physics == TEST_HOTKEY_CATCHUP_FRAMES);
			assert(hotkey_script.exit_renders == 1);
		}
		memset(&hotkey_script, 0, sizeof(hotkey_script));
	}
}

static void assert_presentation_timestamps(legacy_u16 samples_per_second,
										   legacy_u16 distance_per_sample)
{
	assert(frames == 61U && scheduled_snapshots == frames);
	legacy_u64 interval = PRESENTATION_SECOND_NS / samples_per_second;
	const legacy_u16 slots_per_sample = PRESENTATION_RATE / samples_per_second;
	for (legacy_u32 index = 0; index < frames; index++) {
		legacy_u64 time = scheduled_renders[index].time;
		/* The frame loop sleeps in whole milliseconds. It must never present
		 * before the exact rational 60 Hz deadline. */
		assert(time == (index * 1000U + PRESENTATION_RATE - 1U) / PRESENTATION_RATE * 1000000ULL);
		legacy_u32 sample = (legacy_u32)(time / interval);
		legacy_s32 expected = 0;
		if (sample != 0) {
			/* Display evenly spaced points in the completed authoritative interval,
			 * ending at its newest state one visual slot before the next sample. */
			legacy_u32 fraction =
				(index % slots_per_sample + 1U) * FRAME_INTERPOLATION_ONE / slots_per_sample;
			expected = (sample - 1U) * distance_per_sample +
					   distance_per_sample * fraction / FRAME_INTERPOLATION_ONE;
		}
		assert(scheduled_renders[index].position == expected);
		assert(scheduled_renders[index].authoritative_position ==
			   (legacy_s32)sample * distance_per_sample);
		assert(scheduled_renders[index].frame * 60L ==
			   scheduled_renders[index].authoritative_position);
	}
}

static void test_presentation_rate(void)
{
	for (legacy_u16 rate = 10; rate <= 20; rate += 10) {
		struct GAMESTATE baseline;
		for (legacy_u16 mode = 0; mode < 3; mode++) {
			struct RACE_VIEWPORT_CACHE cache = {-1, -1, 0};
			prepare_presentation_test(rate, mode);
			race_run_frames(&cache);
			assert(scheduled_physics == rate);
			assert(keys == rate);
			assert(scheduled_ghosts == rate + 1U);
			assert((legacy_u16)state.game_frame == rate);
			if (mode == 0) {
				baseline = state;
				assert(frames == rate + 1U && scheduled_snapshots == 0);
			} else {
				assert(memcmp(&baseline, &state, sizeof(state)) == 0);
				assert(scheduled_snapshots != 0);
				if (mode == 1) {
					assert_presentation_timestamps(rate, 60);
				} else {
					assert(frames > rate + 1U && frames < 61U);
				}
			}
		}
	}
}

static void test_replay_presentation_rate(void)
{
	static const legacy_s16 speeds[] = {REPLAY_PLAYBACK_NORMAL, REPLAY_PLAYBACK_SLOW,
										REPLAY_PLAYBACK_FAST};
	for (legacy_u16 rate = 10; rate <= 20; rate += 10) {
		for (legacy_u16 speed = 0; speed < 3; speed++) {
			struct RACE_VIEWPORT_CACHE cache = {-1, -1, 0};
			prepare_presentation_test(rate, 1);
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			replay_playback_speed = speeds[speed];
			race_run_frames(&cache);
			legacy_u16 control_samples = speed == 1 ? rate / 2 : rate;
			legacy_u16 physics_steps = speed == 2 ? rate * 2 : control_samples;
			assert(scheduled_physics == physics_steps);
			assert(keys == control_samples);
			assert(scheduled_ghosts == control_samples + 1U);
			assert((legacy_u16)state.game_frame == physics_steps);
			assert(state.playerstate.car_position.lx == physics_steps * 60L);
			assert_presentation_timestamps(control_samples, speed == 2 ? 120 : 60);
		}
	}
}

static void prepare_interpolation_pair(void)
{
	prepare_presentation_test(20, 1);
	race_presentation = (struct RACE_PRESENTATION){0};
	race_presentation_sync(0);
	scheduled_time = 50000000U;
	state.game_frame = elapsed_time2 = 1;
	state.playerstate.car_position.lx = 60;
	race_presentation_capture();
	assert(race_presentation.history_valid != 0);
	race_presentation.started = 1;
}

static void assert_visual_position(legacy_s32 position)
{
	struct GAMESTATE authoritative = state;
	legacy_u32 physics = scheduled_physics;
	legacy_u32 input_samples = keys;
	race_draw_visual();
	assert(scheduled_renders[frames - 1U].position == position);
	assert(memcmp(&authoritative, &state, sizeof(state)) == 0);
	assert(scheduled_physics == physics && keys == input_samples);
}

static void test_interpolation_slot_boundaries(void)
{
	for (legacy_u16 rate = 10; rate <= 20; rate += 10) {
		for (legacy_u16 slow = 0; slow < 2; slow++) {
			prepare_interpolation_pair();
			framespersec = rate;
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			replay_playback_speed = slow != 0 ? REPLAY_PLAYBACK_SLOW : REPLAY_PLAYBACK_NORMAL;
			legacy_u16 slots = PRESENTATION_RATE / rate * (slow != 0 ? 2U : 1U);
			legacy_u64 origin = scheduled_time;
			struct GAMESTATE authoritative = state;
			race_present_interpolation();
			assert(frames == 1);
			assert(race_presentation_fraction(scheduled_time) == FRAME_INTERPOLATION_ONE / slots);
			for (legacy_u16 slot = 1; slot < slots; slot++) {
				legacy_u64 deadline =
					origin + ((legacy_u64)slot * PRESENTATION_SECOND_NS + PRESENTATION_RATE - 1U) /
								 PRESENTATION_RATE;
				scheduled_time = deadline - 1U;
				assert(race_presentation_fraction(scheduled_time) ==
					   slot * FRAME_INTERPOLATION_ONE / slots);
				race_present_interpolation();
				assert(frames == slot);
				scheduled_time = deadline;
				legacy_u32 fraction = (slot + 1U) * FRAME_INTERPOLATION_ONE / slots;
				assert(race_presentation_fraction(scheduled_time) == fraction);
				race_present_interpolation();
				assert(frames == slot + 1U);
				assert(scheduled_renders[slot].position ==
					   (legacy_s32)(60U * fraction / FRAME_INTERPOLATION_ONE));
			}
			assert(race_presentation_fraction(scheduled_time) == FRAME_INTERPOLATION_ONE);
			assert(scheduled_renders[frames - 1U].position == 60);
			assert(memcmp(&authoritative, &state, sizeof(state)) == 0);
			assert(scheduled_physics == 0 && keys == 0);
		}
	}
}

static void test_interpolation_slots_and_stalls(void)
{
	prepare_interpolation_pair();
	assert_visual_position(19);
	scheduled_time = 66666666U;
	assert_visual_position(19);
	scheduled_time = 66666667U;
	assert_visual_position(39);
	scheduled_time = 83333333U;
	assert_visual_position(39);
	scheduled_time = 83333334U;
	assert_visual_position(60);
	scheduled_time = 500000000U;
	assert_visual_position(60);

	prepare_interpolation_pair();
	race_present_interpolation();
	assert(frames == 1 && scheduled_renders[0].position == 19);
	/* A late frame skips missed slots without drawing a catch-up burst. */
	scheduled_time = 181000000U;
	race_present_interpolation();
	assert(frames == 2 && scheduled_renders[1].position == 60);
	race_present_interpolation();
	assert(frames == 2);
	scheduled_time = 183333333U;
	race_present_interpolation();
	assert(frames == 2);
	scheduled_time = 183333334U;
	race_present_interpolation();
	assert(frames == 3 && scheduled_renders[2].position == 60);
}

static void test_interpolation_resets(void)
{
	for (legacy_u16 reset = 0; reset < 6; reset++) {
		prepare_interpolation_pair();
		assert_visual_position(19);
		if (reset == 0) {
			cameramode++;
		} else if (reset == 1) {
			supersight_enabled = 0;
			race_presentation_sync(0);
			supersight_enabled = 1;
		} else if (reset == 2) {
			game_replay_mode = REPLAY_MODE_PLAYBACK;
			is_in_replay = 1;
		} else if (reset == 3) {
			custom_camera.distance++;
		} else if (reset == 4) {
			race_presentation_sync(1);
		} else {
			replay_playback_speed = REPLAY_PLAYBACK_FAST;
		}
		race_presentation_sync(0);
		assert(race_presentation.history_valid == 0);
		assert_visual_position(60);
	}
	for (legacy_u16 backwards = 0; backwards < 2; backwards++) {
		prepare_interpolation_pair();
		state.game_frame = elapsed_time2 = backwards != 0 ? 0 : 10;
		state.playerstate.car_position.lx = state.game_frame * 60L;
		race_presentation_capture();
		assert(race_presentation.history_valid == 0);
		assert_visual_position(state.playerstate.car_position.lx);
		state.game_frame = ++elapsed_time2;
		state.playerstate.car_position.lx += 60;
		scheduled_time += 50000000U;
		race_presentation_capture();
		assert(race_presentation.history_valid != 0);
		assert_visual_position(state.playerstate.car_position.lx - 41);
	}
}

static void assert_visual_ghost_position(legacy_s32 position)
{
	struct CARSTATE authoritative_car = scheduled_ghost;
	struct GHOST_CAMERA_STATE authoritative_camera = scheduled_ghost_camera;
	assert_visual_position(state.playerstate.car_position.lx);
	assert(race_presentation.ghost_valid != 0);
	assert(race_presentation.ghost_interpolated.car_position.lx == position);
	assert(race_presentation.ghost_camera_interpolated.follow_position.x == position);
	assert(memcmp(&authoritative_car, &scheduled_ghost, sizeof(authoritative_car)) == 0);
	assert(memcmp(&authoritative_camera, &scheduled_ghost_camera, sizeof(authoritative_camera)) ==
		   0);
}

static void test_ghost_waits_for_race_start(void)
{
	static const legacy_u16 rates[] = {GAME_FRAME_RATE_LOW, GAME_FRAME_RATE_NORMAL};
	static const legacy_u8 sequences[] = {RACE_START_SEQUENCE_FLAG_ANIMATION,
										  RACE_START_SEQUENCE_AUTO_DRIVE};
	for (legacy_u16 rate_index = 0; rate_index < sizeof(rates) / sizeof(rates[0]); rate_index++) {
		legacy_u16 rate = rates[rate_index];
		legacy_u16 slots = PRESENTATION_RATE / rate;
		prepare_presentation_test(rate, 1);
		race_presentation = (struct RACE_PRESENTATION){0};
		scheduled_ghost_active = 1;
		scheduled_ghost = (struct CARSTATE){0};
		scheduled_ghost_camera = (struct GHOST_CAMERA_STATE){0};
		game_replay_mode = REPLAY_MODE_PAUSED;
		state.game_inputmode = GAME_INPUT_MODE_WAITING;
		race_presentation_sync(0);
		/* The truck sequence advances simulation frames while race time stays zero. */
		for (legacy_u16 sequence = 0; sequence < sizeof(sequences) / sizeof(sequences[0]);
			 sequence++) {
			race_start_sequence_state = sequences[sequence];
			state.game_frame++;
			elapsed_time2 = state.game_frame;
			ghost_update(0, rate);
			scheduled_time += PRESENTATION_SECOND_NS / rate;
			race_presentation_capture();
			assert(race_presentation.history_valid != 0);
			for (legacy_u16 slot = 0; slot < slots; slot++) {
				scheduled_time =
					race_presentation.sample_time +
					((legacy_u64)slot * PRESENTATION_SECOND_NS + PRESENTATION_RATE - 1U) /
						PRESENTATION_RATE;
				assert_visual_ghost_position(0);
				assert(scheduled_ghost_sample_frame == 0 && scheduled_ghost_sample_rate == rate);
			}
		}
		/* Finishing the animation still waits for the driver's first input. */
		game_replay_mode = REPLAY_MODE_LIVE;
		race_start_sequence_state = RACE_START_SEQUENCE_INACTIVE;
		state.game_frame = elapsed_time2 = 0;
		ghost_update(0, rate);
		race_presentation_sync(0);
		assert_visual_ghost_position(0);
		scheduled_time += PRESENTATION_SECOND_NS / rate;
		assert_visual_ghost_position(0);

		state.game_inputmode = GAME_INPUT_MODE_ACTIVE;
		state.game_frame = elapsed_time2 = 1;
		ghost_update(1, rate);
		race_presentation_capture();
		assert(race_presentation.history_valid != 0);
		for (legacy_u16 slot = 0; slot < slots; slot++) {
			scheduled_time = race_presentation.sample_time +
							 ((legacy_u64)slot * PRESENTATION_SECOND_NS + PRESENTATION_RATE - 1U) /
								 PRESENTATION_RATE;
			legacy_u32 fraction = (slot + 1U) * FRAME_INTERPOLATION_ONE / slots;
			assert_visual_ghost_position(
				(legacy_s32)(fraction * TEST_GHOST_DISTANCE_PER_FRAME / FRAME_INTERPOLATION_ONE));
			assert(scheduled_ghost_sample_frame == 1 && scheduled_ghost_sample_rate == rate);
		}
	}
	scheduled_ghost_active = 0;
}

static void test_ghost_interpolation_timestamps(void)
{
	prepare_interpolation_pair();
	scheduled_ghost_active = 1;
	scheduled_ghost = (struct CARSTATE){0};
	scheduled_ghost.car_position.lx = 500;
	scheduled_ghost_camera = (struct GHOST_CAMERA_STATE){0};
	scheduled_ghost_camera.follow_position.x = 500;
	elapsed_time1 = 7;
	race_presentation_interpolate_ghost(FRAME_INTERPOLATION_ONE / 2U);
	assert(race_presentation.ghost_valid != 0);
	assert(scheduled_ghost_sample_calls == 1);
	assert(scheduled_ghost_sample_frame == 8 && scheduled_ghost_sample_rate == 20);
	assert(scheduled_ghost_sample_lag == FRAME_INTERPOLATION_ONE / 2U);
	assert(race_presentation.ghost_interpolated.car_position.lx == 750);
	assert(race_presentation.ghost_camera_interpolated.follow_position.x == 750);
	race_presentation_interpolate_ghost(FRAME_INTERPOLATION_ONE);
	assert(scheduled_ghost_sample_lag == 0);
	assert(race_presentation.ghost_interpolated.car_position.lx == 800);

	/* Fast playback spans two live ticks; the same visual fraction must map
	 * to the same recorded time for the ghost as for the player's pose. */
	race_presentation.sample_span = 2;
	race_presentation_interpolate_ghost(FRAME_INTERPOLATION_ONE / 4U);
	assert(scheduled_ghost_sample_lag == FRAME_INTERPOLATION_ONE * 3U / 2U);
	assert(race_presentation.ghost_interpolated.car_position.lx == 650);

	scheduled_ghost_sample_succeeds = 0;
	race_presentation_interpolate_ghost(FRAME_INTERPOLATION_ONE / 2U);
	assert(race_presentation.ghost_valid != 0);
	assert(race_presentation.ghost_interpolated.car_position.lx == 500);
	assert(race_presentation.ghost_camera_interpolated.follow_position.x == 500);
	legacy_u32 reads = scheduled_ghost_sample_calls;
	race_presentation.history_valid = 0;
	scheduled_ghost_sample_succeeds = 1;
	race_presentation_interpolate_ghost(FRAME_INTERPOLATION_ONE / 2U);
	assert(scheduled_ghost_sample_calls == reads);
	assert(race_presentation.ghost_interpolated.car_position.lx == 500);
	assert(scheduled_ghost.car_position.lx == 500);
	assert(scheduled_ghost_camera.follow_position.x == 500);
	scheduled_ghost_active = 0;
	race_presentation_interpolate_ghost(FRAME_INTERPOLATION_ONE / 2U);
	assert(race_presentation.ghost_valid == 0);
	assert(scheduled_ghost_sample_calls == reads);
}

#endif

int main(void)
{
#ifdef RESTUNTS_SDL3
	replay_input_buffer = scheduled_input;
#endif
	struct RACE_VIEWPORT_CACHE cache;
	for (scenario = 0; scenario < 192; scenario++) {
		trace(scenario);
		memset(&state, 0, sizeof(state));
		memset(&gameconfig, 0, sizeof(gameconfig));
		memset(&rect_windshield, 0, sizeof(rect_windshield));
		memset(&dirty_rect, 0, sizeof(dirty_rect));
		cache.roof_height = cache.dashboard_bottom = -1;
		cache.supersight = 0;
		frames = keys = 0;
		state.game_frame = 10;
		elapsed_time2 = (scenario & 1) ? 11 : 10;
		gameconfig.game_recordedframes = 40;
		state.game_inputmode = (scenario & 2) ? GAME_INPUT_MODE_ACTIVE : GAME_INPUT_MODE_WAITING;
		game_replay_mode = (scenario % 3) == 0	 ? REPLAY_MODE_LIVE
						   : (scenario % 3) == 1 ? REPLAY_MODE_PLAYBACK
												 : REPLAY_MODE_PAUSED;
		game_replay_mode_copy = -1;
		race_exit_request = scenario & 4 ? 1 : 0;
		race_start_sequence_state =
			scenario & 8 ? RACE_START_SEQUENCE_INACTIVE : RACE_START_SEQUENCE_FLAG_ANIMATION;
		idle_expired = scenario & 16;
		video_uses_page_flipping = scenario & 32;
		video_page_count = 2;
		slow_video_mgmt = scenario & 64;
		slow_video_mgmt_copy = -1;
		mouse_driving_enabled = scenario & 1;
		recording_limit_warning_requested = scenario & 128;
		frame_buffer_index = dashboard_buffer_index = 0;
		dashb_toggle = scenario & 2;
		replaybar_toggle = scenario & 4;
		is_in_replay = scenario & 8;
		followOpponentFlag = scenario & 1;
		dashbmp_y = 140;
		roofbmpheight = 12;
		dastbmp_y = 130;
		full_redraw_frames_remaining = 1;
		height_above_replaybar = 200;
		viewport_bottom_cache = -1;
		active_frame_rects = &dirty_rect;
		configured_frame_rate = 20;
		race_run_frames(&cache);
		trace(frames);
		trace(keys);
		trace(game_replay_mode);
		trace(race_exit_request);
		trace(state.game_frame);
		trace(elapsed_time2);
		trace(gameconfig.game_recordedframes);
		trace(frame_buffer_index);
		trace(dashboard_buffer_index);
		trace(full_redraw_frames_remaining);
		trace(recording_limit_warning_requested);
		trace(race_start_sequence_state);
		trace(is_in_replay);
	}
#ifdef RACE_FRAMES_RECORD_BASELINE
	printf("Race frame fingerprint: %08" LEGACY_PRIx32 "\n", trace_hash);
#else
	assert(trace_hash == UINT32_C(0xe6335ceb));
#endif
	test_driving_hotkey_queue();
	test_rewind_frame_loop();
	test_replay_dashboard_rendering();
#ifdef RESTUNTS_SDL3
	test_render_scale_transitions();
	test_controller_start_from_truck();
	test_controller_menu_transition();
	test_hotkeys_during_frame_waits();
	test_hotkey_exit_during_catchup();
	test_presentation_rate();
	test_replay_presentation_rate();
	test_interpolation_slot_boundaries();
	test_interpolation_slots_and_stalls();
	test_interpolation_resets();
	test_ghost_waits_for_race_start();
	test_ghost_interpolation_timestamps();
#endif
	return 0;
}
