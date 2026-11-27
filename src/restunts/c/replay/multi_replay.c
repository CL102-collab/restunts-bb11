#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "multi_replay.h"

#include "multi_replay_capture.h"
#include "../hires/frame_interpolation.h"
#include "multi_replay_manifest.h"
#include "../car/car_audio.h"
#include "../renderer/frame_overlay.h"
#include "../video/video_frame.h"

struct MULTI_REPLAY_ENTRY {
	const legacy_s8 *path;
	struct MULTI_REPLAY_CAPTURE capture;
};

struct MULTI_REPLAY_CAR_PROBE {
	legacy_s8 id[REPLAY_CAR_ID_SIZE];
	legacy_u8 available;
	legacy_u8 dashboard_available;
};

struct MULTI_REPLAY_SESSION {
	legacy_char *manifest;
	struct MULTI_REPLAY_ENTRY *entries;
	struct MULTI_REPLAY_CAR *cars;
	size_t entry_count;
	size_t car_count;
	size_t target;
	legacy_u32 frame;
	legacy_u32 input_frame;
	legacy_u32 duration;
	legacy_u8 active;
};

static struct MULTI_REPLAY_SESSION session;

static void multi_replay_set_filename(const legacy_s8 *path)
{
	const legacy_char *filename = (const legacy_char *)path;
	const legacy_char *separator = strrchr(filename, '/');
#ifdef _WIN32
	const legacy_char *backslash = strrchr(filename, '\\');
	if (backslash != NULL && (separator == NULL || backslash > separator)) {
		separator = backslash;
	}
#endif
	if (separator != NULL) {
		filename = separator + 1;
	}
	const legacy_char *suffix = strrchr(filename, '.');
	size_t length = suffix == NULL ? strlen(filename) : (size_t)(suffix - filename);
	if (length >= REPLAY_FILENAME_SIZE) {
		length = REPLAY_FILENAME_SIZE - 1U;
	}
	memcpy(replay_filename, filename, length);
	replay_filename[length] = 0;
}

static legacy_int multi_replay_error(legacy_char *error, size_t error_size,
									 const legacy_char *message)
{
	snprintf(error, error_size, "%s", message);
	return 0;
}

static legacy_int multi_replay_read_manifest(const legacy_s8 *path, legacy_char *error,
											 size_t error_size)
{
	size_t size;
	session.manifest = SDL_LoadFile((const legacy_char *)path, &size);
	const size_t header_size = sizeof(MULTI_REPLAY_MANIFEST_HEADER) - 1U;
	if (session.manifest == NULL || size < header_size ||
		memcmp(session.manifest, MULTI_REPLAY_MANIFEST_HEADER, header_size) != 0) {
		return multi_replay_error(error, error_size, "Unable to read the multi-replay selection.");
	}

	/* The launcher creates this file for a single launch. */
	if (!SDL_RemovePath((const legacy_char *)path)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not remove the multi-replay selection: %s",
					SDL_GetError());
	}

	const legacy_char *start = session.manifest + header_size;
	const legacy_char *end = session.manifest + size;
	size_t count = 0;
	for (const legacy_char *path_begin = start; path_begin < end;) {
		const legacy_char *next = memchr(path_begin, '\0', (size_t)(end - path_begin));
		if (next == NULL || next == path_begin) {
			return multi_replay_error(error, error_size, "Invalid multi-replay selection.");
		}
		count++;
		path_begin = next + 1;
	}
	if (count == 0U) {
		return multi_replay_error(error, error_size, "Select at least one replay.");
	}
	if (count > SIZE_MAX / sizeof(*session.entries) ||
		count > SIZE_MAX / sizeof(*session.cars) / MULTI_REPLAY_CARS_PER_RECORDING) {
		return multi_replay_error(error, error_size, "The replay selection is too large.");
	}
	session.entries = calloc(count, sizeof(*session.entries));
	session.cars = calloc(count * MULTI_REPLAY_CARS_PER_RECORDING, sizeof(*session.cars));
	if (session.entries == NULL || session.cars == NULL) {
		return multi_replay_error(error, error_size, "Not enough memory for the replay selection.");
	}

	for (size_t index = 0; index < count; index++) {
		session.entries[index].path = (const legacy_s8 *)start;
		start += strlen(start) + 1U;
	}
	session.entry_count = count;
	return 1;
}

static const struct MULTI_REPLAY_CAR_PROBE *
multi_replay_probe_car(struct MULTI_REPLAY_CAR_PROBE *probes, size_t *count, const legacy_s8 *id)
{
	for (size_t index = 0; index < *count; index++) {
		if (SDL_strncasecmp((const legacy_char *)probes[index].id, (const legacy_char *)id,
							REPLAY_CAR_ID_SIZE) == 0) {
			return &probes[index];
		}
	}
	struct MULTI_REPLAY_CAR_PROBE *probe = &probes[(*count)++];
	memcpy(probe->id, id, REPLAY_CAR_ID_SIZE);
	probe->available = multi_replay_car_available(id);
	probe->dashboard_available = probe->available && multi_replay_car_has_dashboard(id);
	return probe;
}

static legacy_int multi_replay_report_missing_cars(const struct MULTI_REPLAY_CAR_PROBE *probes,
												   size_t count, legacy_char *error,
												   size_t error_size)
{
	static const legacy_char prefix[] = "Car files missing for ";
	static const legacy_char suffix[] =
		". Install the car files in the cars subfolder of the selected game-data folder.";
	static const legacy_char omitted[] = ", and others";
	const size_t reserved = sizeof(prefix) + sizeof(suffix) + sizeof(omitted) - 2U;
	const size_t room = error_size > reserved ? error_size - reserved : 0U;
	size_t used = 0;
	legacy_int truncated = 0;

	for (size_t index = 0; index < count; index++) {
		if (probes[index].available) {
			continue;
		}
		const legacy_char *id = (const legacy_char *)probes[index].id;
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Multi-replay: missing car resources for %.*s.",
					(legacy_int)REPLAY_CAR_ID_SIZE, id);
		const legacy_char *separator = used == 0U ? "" : ", ";
		const size_t item_size = strlen(separator) + REPLAY_CAR_ID_SIZE;
		if (room - used < item_size) {
			truncated = 1;
			continue;
		}
		if (used == 0U) {
			memcpy(error, prefix, sizeof(prefix) - 1U);
		}
		snprintf(error + sizeof(prefix) - 1U + used, room - used + 1U, "%s%.*s", separator,
				 (legacy_int)REPLAY_CAR_ID_SIZE, id);
		used += item_size;
	}
	if (used == 0U) {
		return multi_replay_error(error, error_size,
								  "Missing car resources. Install car files "
								  "in the game-data cars subfolder.");
	}
	const size_t end = sizeof(prefix) - 1U + used;
	snprintf(error + end, error_size - end, "%s%s", truncated ? omitted : "", suffix);
	return 0;
}

static legacy_int multi_replay_capture_entries(legacy_char *error, size_t error_size)
{
	struct MULTI_REPLAY_CAR_PROBE *probes =
		calloc(session.entry_count * MULTI_REPLAY_CARS_PER_RECORDING, sizeof(*probes));
	if (probes == NULL) {
		return multi_replay_error(error, error_size, "Not enough memory to check replay cars.");
	}
	struct MULTI_REPLAY_SOURCE primary = {0};
	size_t probed = 0;
	legacy_int all_cars_available = 1;
	legacy_int valid = 1;
	for (size_t index = 0; index < session.entry_count && valid; index++) {
		struct MULTI_REPLAY_ENTRY *entry = &session.entries[index];
		struct MULTI_REPLAY_SOURCE current = {0};
		struct MULTI_REPLAY_SOURCE *source = index == 0U ? &primary : &current;
		valid = multi_replay_source_read(entry->path, source, error, error_size);
		if (valid && index != 0U && !multi_replay_source_same_track(&primary, source)) {
			valid = multi_replay_error(error, error_size,
									   "The selected replays no longer use the same track.");
		}
		const struct MULTI_REPLAY_CAR_PROBE *cars[MULTI_REPLAY_CARS_PER_RECORDING] = {0};
		if (valid) {
			const struct GAMEINFO *config = &source->config;
			cars[PLAYER_CAR_INDEX] =
				multi_replay_probe_car(probes, &probed, config->game_playercarid);
			all_cars_available &= cars[PLAYER_CAR_INDEX]->available;
			if (config->game_opponenttype != 0) {
				cars[OPPONENT_CAR_INDEX] =
					multi_replay_probe_car(probes, &probed, config->game_opponentcarid);
				all_cars_available &= cars[OPPONENT_CAR_INDEX]->available;
			}
		}
		if (valid && all_cars_available && index == 0U) {
			valid = multi_replay_source_activate_track(source, error, error_size);
		}
		if (valid && all_cars_available) {
			valid = multi_replay_capture(source, &entry->capture, error, error_size);
		}
		multi_replay_source_free(&current);
		if (!valid) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Multi-replay: %s (%s)", error, entry->path);
			break;
		}
		if (!all_cars_available) {
			continue;
		}
		const struct MULTI_REPLAY_CAPTURE *capture = &entry->capture;
		legacy_u64 duration = (legacy_u64)capture->frame_count * MULTI_REPLAY_FRAME_RATE /
							  capture->config.game_framespersec;
		if (duration > LEGACY_U32_MAX) {
			valid = multi_replay_error(error, error_size, "A selected replay is too long.");
			break;
		}
		if (duration > session.duration) {
			session.duration = (legacy_u32)duration;
		}

		for (legacy_u8 car_index = 0; car_index < capture->car_count; car_index++) {
			struct MULTI_REPLAY_CAR *car = &session.cars[session.car_count++];
			car->simd = capture->simd[car_index];
			car->frame_rate = capture->config.game_framespersec;
			car->dashboard_available = cars[car_index]->dashboard_available;
			if (car_index == PLAYER_CAR_INDEX) {
				memcpy(car->id, capture->config.game_playercarid, REPLAY_CAR_ID_SIZE);
				car->material = capture->config.game_playermaterial;
			} else {
				memcpy(car->id, capture->config.game_opponentcarid, REPLAY_CAR_ID_SIZE);
				car->material = capture->config.game_opponentmaterial;
			}
		}
	}
	multi_replay_source_free(&primary);
	if (valid && !all_cars_available) {
		valid = multi_replay_report_missing_cars(probes, probed, error, error_size);
	}
	free(probes);
	return valid;
}

legacy_int multi_replay_open(const legacy_s8 *manifest_path, legacy_char *error, size_t error_size)
{
	multi_replay_close();
	if (!multi_replay_read_manifest(manifest_path, error, error_size) ||
		!multi_replay_capture_entries(error, error_size)) {
		multi_replay_close();
		return 0;
	}

	gameconfig = session.entries[0].capture.config;
	gameconfig.game_framespersec = MULTI_REPLAY_FRAME_RATE;
	gameconfig.game_recordedframes = 0;
	multi_replay_set_filename(session.entries[0].path);
	session.active = 1;
	return 1;
}

void multi_replay_close(void)
{
	for (size_t index = 0; index < session.entry_count; index++) {
		multi_replay_capture_free(&session.entries[index].capture);
	}
	free(session.entries);
	free(session.cars);
	SDL_free(session.manifest);
	memset(&session, 0, sizeof(session));
}

legacy_int multi_replay_is_active(void)
{
	return session.active;
}

size_t multi_replay_car_count(void)
{
	return session.car_count;
}

size_t multi_replay_target_index(void)
{
	return session.target;
}

const struct MULTI_REPLAY_CAR *multi_replay_car(size_t index)
{
	return index < session.car_count ? &session.cars[index] : NULL;
}

const struct MULTI_REPLAY_CAR *multi_replay_target_car(void)
{
	return session.active ? &session.cars[session.target] : NULL;
}

void multi_replay_cycle_target(legacy_s16 direction)
{
	if (session.car_count < 2U) {
		return;
	}
	if (direction < 0) {
		session.target = session.target == 0U ? session.car_count - 1U : session.target - 1U;
	} else {
		session.target = (session.target + 1U) % session.car_count;
	}
	size_t index = session.target;
	for (size_t replay = 0; replay < session.entry_count; replay++) {
		legacy_u8 count = session.entries[replay].capture.car_count;
		if (index < count) {
			multi_replay_set_filename(session.entries[replay].path);
			break;
		}
		index -= count;
	}
	audio_multi_replay_reset();
	full_redraw_frames_remaining = (legacy_s8)video_page_count;
	frame_renderer_reset();
}

static void multi_replay_sample(legacy_u32 frame)
{
	size_t next_car = 0;
	for (size_t index = 0; index < session.entry_count; index++) {
		const struct MULTI_REPLAY_CAPTURE *capture = &session.entries[index].capture;
		const legacy_u64 native_time = (legacy_u64)frame * capture->config.game_framespersec;
		const legacy_u64 last_time = (legacy_u64)capture->frame_count * MULTI_REPLAY_FRAME_RATE;
		const legacy_u8 finished = native_time > last_time;
		const size_t sample_index =
			finished ? capture->frame_count : (size_t)(native_time / MULTI_REPLAY_FRAME_RATE);
		for (legacy_u8 car_index = 0; car_index < capture->car_count; car_index++) {
			struct MULTI_REPLAY_CAR *car = &session.cars[next_car++];
			car->state = capture->frames[sample_index].cars[car_index].car;
			car->camera = capture->frames[sample_index].cars[car_index].camera;
			car->finished = finished;
		}
	}

	/* Keep the first replay in the legacy race state for shared scene effects. */
	const struct MULTI_REPLAY_CAR *player = &session.cars[PLAYER_CAR_INDEX];
	state.playerstate = player->state;
	state.game_follow_camera_position[PLAYER_CAR_INDEX] = player->camera.follow_position;
	state.game_player_camera_previous = player->camera.previous_position;
	state.game_trackside_camera_index[PLAYER_CAR_INDEX] = (legacy_u8)player->camera.trackside_index;
	if (session.entries[0].capture.car_count > OPPONENT_CAR_INDEX) {
		const struct MULTI_REPLAY_CAR *opponent = &session.cars[OPPONENT_CAR_INDEX];
		state.opponentstate = opponent->state;
		state.game_follow_camera_position[OPPONENT_CAR_INDEX] = opponent->camera.follow_position;
		state.game_opponent_camera_previous = opponent->camera.previous_position;
		state.game_trackside_camera_index[OPPONENT_CAR_INDEX] =
			(legacy_u8)opponent->camera.trackside_index;
	}
	session.frame = frame;
	state.game_frame = LEGACY_S16_FROM_BITS((legacy_u16)frame);
}

legacy_u32 multi_replay_frame(void)
{
	return session.frame;
}

legacy_u32 multi_replay_input_frame(void)
{
	return session.input_frame;
}

legacy_u32 multi_replay_recorded_frames(void)
{
	return session.duration;
}

void multi_replay_seek(legacy_u32 frame)
{
	session.input_frame = frame < session.duration ? frame : session.duration;
	multi_replay_sample(session.input_frame);
	audio_multi_replay_reset();
}

legacy_int multi_replay_queue_frame(void)
{
	if (session.input_frame == session.duration) {
		return 0;
	}
	session.input_frame++;
	return 1;
}

void multi_replay_present_input_frame(void)
{
	/* Each native sample can carry a brief audio event. Let the race loop catch
	 * up one tick at a time so it passes each event to the audio driver. */
	if (session.frame < session.input_frame) {
		multi_replay_sample(session.frame + 1U);
		audio_carstate();
	}
}

void multi_replay_interpolate(legacy_u32 lag, struct MULTI_REPLAY_CAR *destination)
{
	legacy_u64 session_time = (legacy_u64)session.frame * FRAME_INTERPOLATION_ONE;
	session_time = session_time > lag ? session_time - lag : 0;

	size_t next_car = 0;
	for (size_t index = 0; index < session.entry_count; index++) {
		const struct MULTI_REPLAY_CAPTURE *capture = &session.entries[index].capture;
		legacy_u64 source_time =
			session_time * capture->config.game_framespersec / MULTI_REPLAY_FRAME_RATE;
		legacy_u64 end_time = (legacy_u64)capture->frame_count * FRAME_INTERPOLATION_ONE;
		if (source_time > end_time) {
			source_time = end_time;
		}
		size_t lower = (size_t)(source_time / FRAME_INTERPOLATION_ONE);
		legacy_u32 fraction = (legacy_u32)(source_time % FRAME_INTERPOLATION_ONE);
		size_t upper = lower + (fraction != 0);
		for (legacy_u8 slot = 0; slot < capture->car_count; slot++) {
			const struct MULTI_REPLAY_CAPTURE_POSE *first = &capture->frames[lower].cars[slot];
			const struct MULTI_REPLAY_CAPTURE_POSE *last = &capture->frames[upper].cars[slot];
			const struct MULTI_REPLAY_CAR *authoritative = &session.cars[next_car++];
			*destination = *authoritative;
			if (first->car.car_crashBmpFlag == last->car.car_crashBmpFlag &&
				last->car.car_crashBmpFlag == authoritative->state.car_crashBmpFlag) {
				struct CARSTATE pose;
				frame_hires_interpolate_car(&pose, &last->car, &first->car, fraction);
				destination->state.car_position = pose.car_position;
				destination->state.car_rotate = pose.car_rotate;
				destination->state.car_steeringAngle = pose.car_steeringAngle;
				memcpy(destination->state.car_suspension_deflection, pose.car_suspension_deflection,
					   sizeof(pose.car_suspension_deflection));
				frame_hires_interpolate_vector(&destination->camera.follow_position,
											   &last->camera.follow_position,
											   &first->camera.follow_position, fraction);
				frame_hires_interpolate_vector(&destination->camera.previous_position,
											   &last->camera.previous_position,
											   &first->camera.previous_position, fraction);
			}
			destination++;
		}
	}
}
