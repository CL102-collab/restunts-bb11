#include "opponent_animation.h"
#include "opponent.h"
#include "asset_path.h"
#include "hires.h"
#include <SDL3/SDL.h>
#include <nestegg/nestegg.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include <stdio.h>
#include <string.h>

#define ANIMATION_PATH_SIZE 1024
#define ANIMATION_MAX_FILE_BYTES (32U * 1024U * 1024U)
#define ANIMATION_MAX_DIMENSION 2048U
#define ANIMATION_NS_PER_SECOND 1000000000ULL
#define ANIMATION_MAX_DURATION (60ULL * ANIMATION_NS_PER_SECOND)
#define ANIMATION_DEFAULT_FRAME_DURATION (ANIMATION_NS_PER_SECOND / 30U)
#define ANIMATION_MAX_PACKETS_PER_FRAME 256U
#define ANIMATION_MAX_FRAMES_PER_DRAW 120U
#define ANIMATION_DECODE_BUDGET_NS (ANIMATION_NS_PER_SECOND / 100U)
#define ANIMATION_CHROMA_DIVISOR 2U
#define ANIMATION_VP8_INTERFRAME_FLAG 1U
#define ANIMATION_MATRIX_BT709 1U
#define ANIMATION_RANGE_FULL 2U
#ifdef __DJGPP__
#define ANIMATION_DIRECTORY "opponent/anim/"
#else
#define ANIMATION_DIRECTORY "opponents/animations/"
#endif

/* Match nestegg's exact callback/pointer ABI: int64_t can be long on LP64,
 * whereas legacy_s64 is long long. Arithmetic still uses the game types. */
typedef int64_t legacy_webm_s64;
typedef uint64_t legacy_webm_u64;

struct OPPONENT_ANIMATION {
	legacy_u8 *bytes;
	size_t size, position;
	nestegg *demux;
	nestegg_packet *packet;
	vpx_codec_ctx_t decoder;
	legacy_u8 decoder_ready, first_timestamp_set, started, ended, have_frame;
	legacy_uint track, chunk, chunks;
	legacy_u64 first_timestamp, packet_timestamp, next_timestamp;
	legacy_u64 frame_duration, packet_step, last_timestamp, last_step, duration, start;
	nestegg_video_params video;
	SDL_Surface *frame;
	legacy_u8 *yuv;
	SDL_Colorspace colorspace;
};

static struct OPPONENT_ANIMATION animation;

static legacy_webm_s64 animation_read(void *buffer, size_t length, void *userdata)
{
	struct OPPONENT_ANIMATION *clip = userdata;
	size_t remaining = clip->size - clip->position;
	if (length > remaining) {
		length = remaining;
	}
	memcpy(buffer, clip->bytes + clip->position, length);
	clip->position += length;
	return (legacy_webm_s64)length;
}

static legacy_int animation_seek(legacy_webm_s64 offset, legacy_int whence, void *userdata)
{
	struct OPPONENT_ANIMATION *clip = userdata;
	legacy_s64 base;
	switch (whence) {
		case NESTEGG_SEEK_SET:
			base = 0;
			break;
		case NESTEGG_SEEK_CUR:
			base = (legacy_s64)clip->position;
			break;
		case NESTEGG_SEEK_END:
			base = (legacy_s64)clip->size;
			break;
		default:
			return -1;
	}
	if (offset < -base || offset > (legacy_s64)clip->size - base) {
		return -1;
	}
	clip->position = (size_t)(base + offset);
	return 0;
}

static legacy_webm_s64 animation_tell(void *userdata)
{
	return (legacy_webm_s64)((struct OPPONENT_ANIMATION *)userdata)->position;
}

static void animation_close_decoder(void)
{
	if (animation.packet != NULL) {
		nestegg_free_packet(animation.packet);
		animation.packet = NULL;
	}
	if (animation.demux != NULL) {
		nestegg_destroy(animation.demux);
		animation.demux = NULL;
	}
	if (animation.decoder_ready) {
		vpx_codec_destroy(&animation.decoder);
		animation.decoder_ready = 0;
	}
}

void opponent_animation_unload(void)
{
	animation_close_decoder();
	SDL_DestroySurface(animation.frame);
	SDL_free(animation.yuv);
	SDL_free(animation.bytes);
	memset(&animation, 0, sizeof(animation));
}

/* One pending compressed packet keeps the displayed frame unchanged until its
 * timestamp is due. Audio is skipped without initializing an audio decoder. */
static legacy_s32 animation_next_packet(void)
{
	if (animation.packet != NULL) {
		nestegg_free_packet(animation.packet);
		animation.packet = NULL;
	}
	for (legacy_uint scanned = 0; scanned < ANIMATION_MAX_PACKETS_PER_FRAME; scanned++) {
		legacy_int result = nestegg_read_packet(animation.demux, &animation.packet);
		if (result <= 0) {
			animation.ended = result == 0;
			return result;
		}
		legacy_uint track;
		if (nestegg_packet_track(animation.packet, &track) != 0) {
			return -1;
		}
		if (track != animation.track) {
			nestegg_free_packet(animation.packet);
			animation.packet = NULL;
			continue;
		}
		legacy_webm_u64 timestamp;
		if (nestegg_packet_tstamp(animation.packet, &timestamp) != 0 ||
			nestegg_packet_count(animation.packet, &animation.chunks) != 0 ||
			animation.chunks == 0 ||
			nestegg_packet_encryption(animation.packet) != NESTEGG_PACKET_HAS_SIGNAL_BYTE_FALSE) {
			return -1;
		}
		if (!animation.first_timestamp_set) {
			animation.first_timestamp = timestamp;
			animation.first_timestamp_set = 1;
		} else if (timestamp < animation.first_timestamp ||
				   timestamp - animation.first_timestamp < animation.next_timestamp) {
			return -1;
		}
		animation.packet_timestamp = timestamp - animation.first_timestamp;
		if (animation.packet_timestamp >= ANIMATION_MAX_DURATION) {
			return -1;
		}
		animation.next_timestamp = animation.packet_timestamp;
		animation.chunk = 0;
		animation.packet_step = animation.frame_duration;
		legacy_webm_u64 duration;
		if (nestegg_packet_duration(animation.packet, &duration) == 0 && duration != 0) {
			animation.packet_step = duration / animation.chunks;
		}
		if (animation.packet_step == 0 ||
			animation.packet_step > ANIMATION_MAX_DURATION / animation.chunks) {
			return -1;
		}
		return 1;
	}
	return -1;
}

static legacy_s32 animation_open_decoder(void)
{
	animation_close_decoder();
	animation.position = 0;
	animation.first_timestamp_set = 0;
	animation.have_frame = 0;
	animation.ended = 0;
	animation.next_timestamp = 0;
	animation.last_timestamp = 0;
	animation.last_step = ANIMATION_DEFAULT_FRAME_DURATION;
	nestegg_io io = {animation_read, animation_seek, animation_tell, &animation};
	if (nestegg_init(&animation.demux, io, NULL, (legacy_webm_s64)animation.size) != 0) {
		return 0;
	}
	legacy_uint tracks;
	if (nestegg_track_count(animation.demux, &tracks) != 0) {
		return 0;
	}
	for (animation.track = 0; animation.track < tracks; animation.track++) {
		if (nestegg_track_type(animation.demux, animation.track) == NESTEGG_TRACK_VIDEO &&
			nestegg_track_codec_id(animation.demux, animation.track) == NESTEGG_CODEC_VP8) {
			break;
		}
	}
	if (animation.track == tracks ||
		nestegg_track_video_params(animation.demux, animation.track, &animation.video) != 0 ||
		nestegg_track_encoding(animation.demux, animation.track) == NESTEGG_ENCODING_ENCRYPTION ||
		animation.video.width == 0 || animation.video.height == 0 ||
		animation.video.width > ANIMATION_MAX_DIMENSION ||
		animation.video.height > ANIMATION_MAX_DIMENSION) {
		return 0;
	}
	legacy_webm_u64 duration = 0;
	if (nestegg_duration(animation.demux, &duration) == 0 && duration > ANIMATION_MAX_DURATION) {
		return 0;
	}
	animation.duration = duration;
	animation.frame_duration = ANIMATION_DEFAULT_FRAME_DURATION;
	if (nestegg_track_default_duration(animation.demux, animation.track, &duration) == 0 &&
		duration != 0) {
		if (duration > ANIMATION_MAX_DURATION) {
			return 0;
		}
		animation.frame_duration = duration;
	}
	animation.last_step = animation.frame_duration;
	legacy_u8 full_range = animation.video.range == ANIMATION_RANGE_FULL;
	animation.colorspace =
		animation.video.matrix_coefficients == ANIMATION_MATRIX_BT709
			? (full_range ? SDL_COLORSPACE_BT709_FULL : SDL_COLORSPACE_BT709_LIMITED)
			: (full_range ? SDL_COLORSPACE_BT601_FULL : SDL_COLORSPACE_BT601_LIMITED);
	vpx_codec_dec_cfg_t config = {0};
	config.threads = 1;
	config.w = animation.video.width;
	config.h = animation.video.height;
	if (vpx_codec_dec_init(&animation.decoder, vpx_codec_vp8_dx(), &config, 0) != VPX_CODEC_OK) {
		return 0;
	}
	animation.decoder_ready = 1;
	if (animation_next_packet() != 1) {
		return 0;
	}
	if (animation.duration > animation.first_timestamp) {
		animation.duration -= animation.first_timestamp;
	} else {
		animation.duration = 0;
	}
	return 1;
}

static legacy_s32 animation_copy_frame(const vpx_image_t *image)
{
	if ((image->fmt != VPX_IMG_FMT_I420 && image->fmt != VPX_IMG_FMT_YV12) ||
		image->d_w != animation.video.width || image->d_h != animation.video.height) {
		return 0;
	}
	legacy_uint width = image->d_w;
	legacy_uint height = image->d_h;
	legacy_uint chroma_width = (width + ANIMATION_CHROMA_DIVISOR - 1) / ANIMATION_CHROMA_DIVISOR;
	legacy_uint chroma_height = (height + ANIMATION_CHROMA_DIVISOR - 1) / ANIMATION_CHROMA_DIVISOR;
	if (animation.frame == NULL) {
		animation.frame = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
		animation.yuv =
			SDL_malloc(width * height + ANIMATION_CHROMA_DIVISOR * chroma_width * chroma_height);
		if (animation.frame == NULL || animation.yuv == NULL) {
			return 0;
		}
	}
	legacy_u8 *destination = animation.yuv;
	for (legacy_uint plane = VPX_PLANE_Y; plane <= VPX_PLANE_V; plane++) {
		legacy_uint plane_width = plane == VPX_PLANE_Y ? width : chroma_width;
		legacy_uint plane_height = plane == VPX_PLANE_Y ? height : chroma_height;
		for (legacy_uint row = 0; row < plane_height; row++) {
			memcpy(destination, image->planes[plane] + row * image->stride[plane], plane_width);
			destination += plane_width;
		}
	}
	return SDL_ConvertPixelsAndColorspace(width, height, SDL_PIXELFORMAT_IYUV, animation.colorspace,
										  0, animation.yuv, width, SDL_PIXELFORMAT_ARGB8888,
										  SDL_COLORSPACE_SRGB, 0, animation.frame->pixels,
										  animation.frame->pitch);
}

static legacy_s32 animation_decode_frame(void)
{
	legacy_u8 *data;
	size_t size;
	if (nestegg_packet_data(animation.packet, animation.chunk, &data, &size) != 0 || size == 0 ||
		size > ANIMATION_MAX_FILE_BYTES) {
		return 0;
	}
	/* The bitstream can disagree with the container. Check every keyframe before
 * libvpx allocates reference pictures, including resolution changes mid-clip. */
	if ((data[0] & ANIMATION_VP8_INTERFRAME_FLAG) == 0) {
		vpx_codec_stream_info_t info = {0};
		info.sz = sizeof(info);
		if (vpx_codec_peek_stream_info(vpx_codec_vp8_dx(), data, (legacy_uint)size, &info) !=
				VPX_CODEC_OK ||
			info.w != animation.video.width || info.h != animation.video.height) {
			return 0;
		}
	}
	if (vpx_codec_decode(&animation.decoder, data, (legacy_uint)size, NULL, 0) != VPX_CODEC_OK) {
		return 0;
	}
	legacy_int corrupted = 0;
	if (vpx_codec_control(&animation.decoder, VP8D_GET_FRAME_CORRUPTED, &corrupted) !=
			VPX_CODEC_OK ||
		corrupted != 0) {
		return 0;
	}
	vpx_codec_iter_t iterator = NULL;
	vpx_image_t *image;
	while ((image = vpx_codec_get_frame(&animation.decoder, &iterator)) != NULL) {
		if (!animation_copy_frame(image)) {
			return 0;
		}
		animation.have_frame = 1;
	}
	if (animation.next_timestamp > animation.last_timestamp) {
		animation.last_step = animation.next_timestamp - animation.last_timestamp;
	}
	animation.last_timestamp = animation.next_timestamp;
	animation.chunk++;
	if (animation.chunk < animation.chunks) {
		animation.next_timestamp =
			animation.packet_timestamp + animation.packet_step * animation.chunk;
		return animation.next_timestamp < ANIMATION_MAX_DURATION;
	}
	return animation_next_packet() >= 0;
}

static legacy_s32 animation_first_frame(void)
{
	if (!animation_open_decoder()) {
		return 0;
	}
	for (legacy_uint frames = 0; frames < ANIMATION_MAX_FRAMES_PER_DRAW; frames++) {
		if (!animation_decode_frame()) {
			return 0;
		}
		if (animation.have_frame) {
			return 1;
		}
		if (animation.ended) {
			break;
		}
	}
	return 0;
}

static legacy_s32 animation_load_path(const legacy_char *directory, legacy_u8 opponent,
									  legacy_u8 won)
{
	legacy_char path[ANIMATION_PATH_SIZE];
	legacy_s32 length = snprintf(path, sizeof(path), "%sopp%u%s.webm", directory,
								 (legacy_uint)opponent, won ? "win" : "lose");
	if (length < 0 || (size_t)length >= sizeof(path)) {
		return 0;
	}
	FILE *file = asset_path_open_file(path, "rb");
	if (file == NULL) {
		return 0;
	}
	legacy_s64 size = -1;
	if (fseek(file, 0, SEEK_END) == 0) {
		size = ftell(file);
	}
	if (size <= 0 || size > ANIMATION_MAX_FILE_BYTES || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return 0;
	}
	animation.bytes = SDL_malloc((size_t)size);
	animation.size = (size_t)size;
	legacy_u8 loaded = animation.bytes != NULL &&
					   fread(animation.bytes, 1, animation.size, file) == animation.size;
	fclose(file);
	if (!loaded || !nestegg_sniff_webm(animation.bytes, animation.size) ||
		!animation_first_frame()) {
		opponent_animation_unload();
		return 0;
	}
	return 1;
}

void opponent_animation_load(legacy_u8 opponent, legacy_u8 won)
{
	opponent_animation_unload();
	if (opponent < OPPONENT_FIRST || opponent > OPPONENT_LAST) {
		return;
	}
	if (animation_load_path(ANIMATION_DIRECTORY, opponent, won)) {
		return;
	}
	const legacy_char *base = asset_path_base();
	if (base != NULL) {
		legacy_char directory[ANIMATION_PATH_SIZE];
		legacy_s32 length =
			snprintf(directory, sizeof(directory), "%s%s", base, ANIMATION_DIRECTORY);
		if (length >= 0 && (size_t)length < sizeof(directory) &&
			animation_load_path(directory, opponent, won)) {
			return;
		}
	}
#ifdef RESTUNTS_OPPONENT_DIRECTORY
	(void)animation_load_path(RESTUNTS_OPPONENT_DIRECTORY "/animations/", opponent, won);
#endif
}

static legacy_s32 animation_advance(legacy_u64 now)
{
	if (!animation.started) {
		animation.started = 1;
		animation.start = now;
	}
	legacy_u64 elapsed = now - animation.start;
	for (legacy_uint frames = 0; frames < ANIMATION_MAX_FRAMES_PER_DRAW; frames++) {
		if (animation.ended) {
			legacy_u64 duration = animation.duration;
			if (duration <= animation.last_timestamp) {
				duration = animation.last_timestamp + animation.last_step;
			}
			if (duration > ANIMATION_MAX_DURATION) {
				duration = ANIMATION_MAX_DURATION;
			}
			if (elapsed < duration) {
				return 1;
			}
			elapsed %= duration;
			animation.start = now - elapsed;
			if (!animation_first_frame()) {
				return 0;
			}
		} else if (elapsed < animation.next_timestamp) {
			return 1;
		} else if (!animation_decode_frame()) {
			return 0;
		}
		if (SDL_GetTicksNS() - now >= ANIMATION_DECODE_BUDGET_NS) {
			break;
		}
	}
	/* Keep input responsive after long pauses or for excessively dense clips.
	 * Resume from the last decoded timestamp instead of decoding without bound. */
	animation.start = now - animation.last_timestamp;
	return 1;
}

legacy_s32 opponent_animation_draw(const struct SPRITE *target, legacy_s16 x, legacy_s16 y,
								   legacy_s16 width, legacy_s16 height)
{
	if (!hires_enabled() || animation.frame == NULL || target == NULL || width <= 0 ||
		height <= 0) {
		return 0;
	}
	if (!animation_advance(SDL_GetTicksNS())) {
		opponent_animation_unload();
		return 0;
	}
	if (!hires_begin_argb(target)) {
		return 0;
	}
	legacy_s32 scale = hires_render_scale();
	legacy_s32 output_width = width * scale;
	legacy_s32 output_height = height * scale;
	for (legacy_s32 row = 0; row < output_height; row++) {
		legacy_s32 source_row = row * animation.frame->h / output_height;
		const legacy_u32 *pixels = (const legacy_u32 *)((const legacy_u8 *)animation.frame->pixels +
														source_row * animation.frame->pitch);
		for (legacy_s32 column = 0; column < output_width; column++) {
			legacy_s32 source_column = column * animation.frame->w / output_width;
			hires_argb_pixel(x * scale + column, y * scale + row, pixels[source_column]);
		}
	}
	hires_end();
	return 1;
}
