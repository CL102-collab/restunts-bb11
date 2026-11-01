#include "opponent_artwork.h"
#include "../opponent/opponent.h"
#include "../asset_path.h"
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

static struct OPPONENT_ANIMATION hires_animation;

static legacy_webm_s64 hires_animation_read(void *buffer, size_t length, void *userdata)
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

static legacy_int hires_animation_seek(legacy_webm_s64 offset, legacy_int whence, void *userdata)
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

static legacy_webm_s64 hires_animation_tell(void *userdata)
{
	return (legacy_webm_s64)((struct OPPONENT_ANIMATION *)userdata)->position;
}

static void hires_animation_close_decoder(void)
{
	if (hires_animation.packet != NULL) {
		nestegg_free_packet(hires_animation.packet);
		hires_animation.packet = NULL;
	}
	if (hires_animation.demux != NULL) {
		nestegg_destroy(hires_animation.demux);
		hires_animation.demux = NULL;
	}
	if (hires_animation.decoder_ready) {
		vpx_codec_destroy(&hires_animation.decoder);
		hires_animation.decoder_ready = 0;
	}
}

void hires_opponent_animation_unload(void)
{
	hires_animation_close_decoder();
	SDL_DestroySurface(hires_animation.frame);
	SDL_free(hires_animation.yuv);
	SDL_free(hires_animation.bytes);
	memset(&hires_animation, 0, sizeof(hires_animation));
}

/* One pending compressed packet keeps the displayed frame unchanged until its
 * timestamp is due. Audio is skipped without initializing an audio decoder. */
static legacy_s32 hires_animation_next_packet(void)
{
	if (hires_animation.packet != NULL) {
		nestegg_free_packet(hires_animation.packet);
		hires_animation.packet = NULL;
	}
	for (legacy_uint scanned = 0; scanned < ANIMATION_MAX_PACKETS_PER_FRAME; scanned++) {
		legacy_int result = nestegg_read_packet(hires_animation.demux, &hires_animation.packet);
		if (result <= 0) {
			hires_animation.ended = result == 0;
			return result;
		}
		legacy_uint track;
		if (nestegg_packet_track(hires_animation.packet, &track) != 0) {
			return -1;
		}
		if (track != hires_animation.track) {
			nestegg_free_packet(hires_animation.packet);
			hires_animation.packet = NULL;
			continue;
		}
		legacy_webm_u64 timestamp;
		if (nestegg_packet_tstamp(hires_animation.packet, &timestamp) != 0 ||
			nestegg_packet_count(hires_animation.packet, &hires_animation.chunks) != 0 ||
			hires_animation.chunks == 0 ||
			nestegg_packet_encryption(hires_animation.packet) !=
				NESTEGG_PACKET_HAS_SIGNAL_BYTE_FALSE) {
			return -1;
		}
		if (!hires_animation.first_timestamp_set) {
			hires_animation.first_timestamp = timestamp;
			hires_animation.first_timestamp_set = 1;
		} else if (timestamp < hires_animation.first_timestamp ||
				   timestamp - hires_animation.first_timestamp < hires_animation.next_timestamp) {
			return -1;
		}
		hires_animation.packet_timestamp = timestamp - hires_animation.first_timestamp;
		if (hires_animation.packet_timestamp >= ANIMATION_MAX_DURATION) {
			return -1;
		}
		hires_animation.next_timestamp = hires_animation.packet_timestamp;
		hires_animation.chunk = 0;
		hires_animation.packet_step = hires_animation.frame_duration;
		legacy_webm_u64 duration;
		if (nestegg_packet_duration(hires_animation.packet, &duration) == 0 && duration != 0) {
			hires_animation.packet_step = duration / hires_animation.chunks;
		}
		if (hires_animation.packet_step == 0 ||
			hires_animation.packet_step > ANIMATION_MAX_DURATION / hires_animation.chunks) {
			return -1;
		}
		return 1;
	}
	return -1;
}

static legacy_s32 hires_animation_open_decoder(void)
{
	hires_animation_close_decoder();
	hires_animation.position = 0;
	hires_animation.first_timestamp_set = 0;
	hires_animation.have_frame = 0;
	hires_animation.ended = 0;
	hires_animation.next_timestamp = 0;
	hires_animation.last_timestamp = 0;
	hires_animation.last_step = ANIMATION_DEFAULT_FRAME_DURATION;
	nestegg_io io = {hires_animation_read, hires_animation_seek, hires_animation_tell,
					 &hires_animation};
	if (nestegg_init(&hires_animation.demux, io, NULL, (legacy_webm_s64)hires_animation.size) !=
		0) {
		return 0;
	}
	legacy_uint tracks;
	if (nestegg_track_count(hires_animation.demux, &tracks) != 0) {
		return 0;
	}
	for (hires_animation.track = 0; hires_animation.track < tracks; hires_animation.track++) {
		if (nestegg_track_type(hires_animation.demux, hires_animation.track) ==
				NESTEGG_TRACK_VIDEO &&
			nestegg_track_codec_id(hires_animation.demux, hires_animation.track) ==
				NESTEGG_CODEC_VP8) {
			break;
		}
	}
	if (hires_animation.track == tracks ||
		nestegg_track_video_params(hires_animation.demux, hires_animation.track,
								   &hires_animation.video) != 0 ||
		nestegg_track_encoding(hires_animation.demux, hires_animation.track) ==
			NESTEGG_ENCODING_ENCRYPTION ||
		hires_animation.video.width == 0 || hires_animation.video.height == 0 ||
		hires_animation.video.width > ANIMATION_MAX_DIMENSION ||
		hires_animation.video.height > ANIMATION_MAX_DIMENSION) {
		return 0;
	}
	legacy_webm_u64 duration = 0;
	if (nestegg_duration(hires_animation.demux, &duration) == 0 &&
		duration > ANIMATION_MAX_DURATION) {
		return 0;
	}
	hires_animation.duration = duration;
	hires_animation.frame_duration = ANIMATION_DEFAULT_FRAME_DURATION;
	if (nestegg_track_default_duration(hires_animation.demux, hires_animation.track, &duration) ==
			0 &&
		duration != 0) {
		if (duration > ANIMATION_MAX_DURATION) {
			return 0;
		}
		hires_animation.frame_duration = duration;
	}
	hires_animation.last_step = hires_animation.frame_duration;
	legacy_u8 full_range = hires_animation.video.range == ANIMATION_RANGE_FULL;
	hires_animation.colorspace =
		hires_animation.video.matrix_coefficients == ANIMATION_MATRIX_BT709
			? (full_range ? SDL_COLORSPACE_BT709_FULL : SDL_COLORSPACE_BT709_LIMITED)
			: (full_range ? SDL_COLORSPACE_BT601_FULL : SDL_COLORSPACE_BT601_LIMITED);
	vpx_codec_dec_cfg_t config = {0};
	config.threads = 1;
	config.w = hires_animation.video.width;
	config.h = hires_animation.video.height;
	if (vpx_codec_dec_init(&hires_animation.decoder, vpx_codec_vp8_dx(), &config, 0) !=
		VPX_CODEC_OK) {
		return 0;
	}
	hires_animation.decoder_ready = 1;
	if (hires_animation_next_packet() != 1) {
		return 0;
	}
	if (hires_animation.duration > hires_animation.first_timestamp) {
		hires_animation.duration -= hires_animation.first_timestamp;
	} else {
		hires_animation.duration = 0;
	}
	return 1;
}

static legacy_s32 hires_animation_copy_frame(const vpx_image_t *image)
{
	if ((image->fmt != VPX_IMG_FMT_I420 && image->fmt != VPX_IMG_FMT_YV12) ||
		image->d_w != hires_animation.video.width || image->d_h != hires_animation.video.height) {
		return 0;
	}
	legacy_uint width = image->d_w;
	legacy_uint height = image->d_h;
	legacy_uint chroma_width = (width + ANIMATION_CHROMA_DIVISOR - 1) / ANIMATION_CHROMA_DIVISOR;
	legacy_uint chroma_height = (height + ANIMATION_CHROMA_DIVISOR - 1) / ANIMATION_CHROMA_DIVISOR;
	if (hires_animation.frame == NULL) {
		hires_animation.frame = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
		hires_animation.yuv =
			SDL_malloc(width * height + ANIMATION_CHROMA_DIVISOR * chroma_width * chroma_height);
		if (hires_animation.frame == NULL || hires_animation.yuv == NULL) {
			return 0;
		}
	}
	legacy_u8 *destination = hires_animation.yuv;
	for (legacy_uint plane = VPX_PLANE_Y; plane <= VPX_PLANE_V; plane++) {
		legacy_uint plane_width = plane == VPX_PLANE_Y ? width : chroma_width;
		legacy_uint plane_height = plane == VPX_PLANE_Y ? height : chroma_height;
		for (legacy_uint row = 0; row < plane_height; row++) {
			memcpy(destination, image->planes[plane] + row * image->stride[plane], plane_width);
			destination += plane_width;
		}
	}
	return SDL_ConvertPixelsAndColorspace(
		width, height, SDL_PIXELFORMAT_IYUV, hires_animation.colorspace, 0, hires_animation.yuv,
		width, SDL_PIXELFORMAT_ARGB8888, SDL_COLORSPACE_SRGB, 0, hires_animation.frame->pixels,
		hires_animation.frame->pitch);
}

static legacy_s32 hires_animation_decode_frame(void)
{
	legacy_u8 *data;
	size_t size;
	if (nestegg_packet_data(hires_animation.packet, hires_animation.chunk, &data, &size) != 0 ||
		size == 0 || size > ANIMATION_MAX_FILE_BYTES) {
		return 0;
	}
	/* The bitstream can disagree with the container. Check every keyframe before
 * libvpx allocates reference pictures, including resolution changes mid-clip. */
	if ((data[0] & ANIMATION_VP8_INTERFRAME_FLAG) == 0) {
		vpx_codec_stream_info_t info = {0};
		info.sz = sizeof(info);
		if (vpx_codec_peek_stream_info(vpx_codec_vp8_dx(), data, (legacy_uint)size, &info) !=
				VPX_CODEC_OK ||
			info.w != hires_animation.video.width || info.h != hires_animation.video.height) {
			return 0;
		}
	}
	if (vpx_codec_decode(&hires_animation.decoder, data, (legacy_uint)size, NULL, 0) !=
		VPX_CODEC_OK) {
		return 0;
	}
	legacy_int corrupted = 0;
	if (vpx_codec_control(&hires_animation.decoder, VP8D_GET_FRAME_CORRUPTED, &corrupted) !=
			VPX_CODEC_OK ||
		corrupted != 0) {
		return 0;
	}
	vpx_codec_iter_t iterator = NULL;
	vpx_image_t *image;
	while ((image = vpx_codec_get_frame(&hires_animation.decoder, &iterator)) != NULL) {
		if (!hires_animation_copy_frame(image)) {
			return 0;
		}
		hires_animation.have_frame = 1;
	}
	if (hires_animation.next_timestamp > hires_animation.last_timestamp) {
		hires_animation.last_step = hires_animation.next_timestamp - hires_animation.last_timestamp;
	}
	hires_animation.last_timestamp = hires_animation.next_timestamp;
	hires_animation.chunk++;
	if (hires_animation.chunk < hires_animation.chunks) {
		hires_animation.next_timestamp =
			hires_animation.packet_timestamp + hires_animation.packet_step * hires_animation.chunk;
		return hires_animation.next_timestamp < ANIMATION_MAX_DURATION;
	}
	return hires_animation_next_packet() >= 0;
}

static legacy_s32 hires_animation_first_frame(void)
{
	if (!hires_animation_open_decoder()) {
		return 0;
	}
	for (legacy_uint frames = 0; frames < ANIMATION_MAX_FRAMES_PER_DRAW; frames++) {
		if (!hires_animation_decode_frame()) {
			return 0;
		}
		if (hires_animation.have_frame) {
			return 1;
		}
		if (hires_animation.ended) {
			break;
		}
	}
	return 0;
}

static legacy_s32 hires_animation_load_path(const legacy_char *directory, legacy_u8 opponent,
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
	hires_animation.bytes = SDL_malloc((size_t)size);
	hires_animation.size = (size_t)size;
	legacy_u8 loaded =
		hires_animation.bytes != NULL &&
		fread(hires_animation.bytes, 1, hires_animation.size, file) == hires_animation.size;
	fclose(file);
	if (!loaded || !nestegg_sniff_webm(hires_animation.bytes, hires_animation.size) ||
		!hires_animation_first_frame()) {
		hires_opponent_animation_unload();
		return 0;
	}
	return 1;
}

void hires_opponent_animation_load(legacy_u8 opponent, legacy_u8 won)
{
	hires_opponent_animation_unload();
	if (opponent < OPPONENT_FIRST || opponent > OPPONENT_LAST) {
		return;
	}
	if (hires_animation_load_path(ANIMATION_DIRECTORY, opponent, won)) {
		return;
	}
	const legacy_char *base = asset_path_base();
	if (base != NULL) {
		legacy_char directory[ANIMATION_PATH_SIZE];
		legacy_s32 length =
			snprintf(directory, sizeof(directory), "%s%s", base, ANIMATION_DIRECTORY);
		if (length >= 0 && (size_t)length < sizeof(directory) &&
			hires_animation_load_path(directory, opponent, won)) {
			return;
		}
	}
#ifdef RESTUNTS_OPPONENT_DIRECTORY
	(void)hires_animation_load_path(RESTUNTS_OPPONENT_DIRECTORY "/animations/", opponent, won);
#endif
}

static legacy_s32 hires_animation_advance(legacy_u64 now)
{
	if (!hires_animation.started) {
		hires_animation.started = 1;
		hires_animation.start = now;
	}
	legacy_u64 elapsed = now - hires_animation.start;
	for (legacy_uint frames = 0; frames < ANIMATION_MAX_FRAMES_PER_DRAW; frames++) {
		if (hires_animation.ended) {
			legacy_u64 duration = hires_animation.duration;
			if (duration <= hires_animation.last_timestamp) {
				duration = hires_animation.last_timestamp + hires_animation.last_step;
			}
			if (duration > ANIMATION_MAX_DURATION) {
				duration = ANIMATION_MAX_DURATION;
			}
			if (elapsed < duration) {
				return 1;
			}
			elapsed %= duration;
			hires_animation.start = now - elapsed;
			if (!hires_animation_first_frame()) {
				return 0;
			}
		} else if (elapsed < hires_animation.next_timestamp) {
			return 1;
		} else if (!hires_animation_decode_frame()) {
			return 0;
		}
		if (SDL_GetTicksNS() - now >= ANIMATION_DECODE_BUDGET_NS) {
			break;
		}
	}
	/* Keep input responsive after long pauses or for excessively dense clips.
	 * Resume from the last decoded timestamp instead of decoding without bound. */
	hires_animation.start = now - hires_animation.last_timestamp;
	return 1;
}

legacy_s32 hires_opponent_animation_draw(const struct SPRITE *target, legacy_s16 x, legacy_s16 y,
										 legacy_s16 width, legacy_s16 height)
{
	if (!hires_enabled() || hires_animation.frame == NULL || target == NULL || width <= 0 ||
		height <= 0) {
		return 0;
	}
	if (!hires_animation_advance(SDL_GetTicksNS())) {
		hires_opponent_animation_unload();
		return 0;
	}
	if (!hires_begin_argb(target)) {
		return 0;
	}
	legacy_s32 scale = hires_render_scale();
	legacy_s32 output_width = width * scale;
	legacy_s32 output_height = height * scale;
	for (legacy_s32 row = 0; row < output_height; row++) {
		legacy_s32 source_row = row * hires_animation.frame->h / output_height;
		const legacy_u32 *pixels =
			(const legacy_u32 *)((const legacy_u8 *)hires_animation.frame->pixels +
								 source_row * hires_animation.frame->pitch);
		for (legacy_s32 column = 0; column < output_width; column++) {
			legacy_s32 source_column = column * hires_animation.frame->w / output_width;
			hires_argb_pixel(x * scale + column, y * scale + row, pixels[source_column]);
		}
	}
	hires_end();
	return 1;
}
