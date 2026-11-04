#include <metal_stdlib>
using namespace metal;

// Same two-buffer wire layout as gpu_raster.comp. Each invocation owns all
// words for one pixel, preserving ordered classic and decal submissions.
constant uint COMMAND_WORDS = 18u;
constant uint EDGE_WORDS = 7u;
constant uint OUTPUT_WORDS = 3u;
constant uint TILE_SIZE = 16u;
constant uint BACKGROUND = 1u;
constant uint DECAL = 2u;
constant uint GROUND_CLIP = 4u;
constant uint LINE = 8u;
constant uint LEGACY = 16u;
constant uint FIXED_HALF = 32768u;
constant uint FIXED_ONE = 65536u;
constant uint PAINT_PATTERN = 1u;
constant uint PAINT_ALTERNATE = 2u;
constant uint TOUCHED = 256u;
constant uint NO_COMMAND = 0xffffffffu;
constant float EPSILON = 0.000000476837158203125f;
constant float DECAL_SCALE = 1.0001f;

struct RasterScene {
	device const uint *scene;
	device uint *image;

	uint commandWord(uint command, uint field)
	{
		return scene[scene[8] + command * COMMAND_WORDS + field];
	}
	float commandFloat(uint command, uint field)
	{
		return as_type<float>(commandWord(command, field));
	}
	float edgeFloat(uint edge, uint field)
	{
		return as_type<float>(scene[scene[9] + edge * EDGE_WORDS + field]);
	}
	uint edgeWord(uint edge, uint field)
	{
		return scene[scene[9] + edge * EDGE_WORDS + field];
	}
	// Each classic edge contains raw legacy line setup: integer x/y,
	// fractional x/y, 16.16 step, mode and inclusive sample count. Integer
	// arithmetic preserves clipping phases and the original fixed rounding.
	bool legacyRow(uint edge, int y, bool line, thread int &left, thread int &right)
	{
		int x0 = int(edgeFloat(edge, 0u)), y0 = int(edgeFloat(edge, 1u));
		uint fx = uint(edgeFloat(edge, 2u)), fy = uint(edgeFloat(edge, 3u));
		uint step = uint(edgeFloat(edge, 4u));
		uint mode = edgeWord(edge, 5u), count = edgeWord(edge, 6u);
		if (count == 0u) {
			return false;
		}
		uint roundedX = ((uint(x0) & 65535u) << 16u) + fx + FIXED_HALF;
		if (mode <= 1u || mode == 9u) {
			if (y != y0 + int((fy + FIXED_HALF) >> 16u)) {
				return false;
			}
			left = int(roundedX) >> 16;
			right = left + int(count) - 1;
			return true;
		}
		if (mode <= 6u) {
			int index = y - y0 - (line ? int((fy + FIXED_HALF) >> 16u) : 0);
			if (index < 0 || uint(index) >= count) {
				return false;
			}
			if (mode == 3u) {
				left = (int(roundedX) >> 16) - index;
			} else if (mode == 4u) {
				left = (int(roundedX) >> 16) + index;
			} else if (mode == 5u) {
				left = int(roundedX - uint(index) * step) >> 16;
			} else if (mode == 6u) {
				left = int(roundedX + uint(index) * step) >> 16;
			} else {
				left = int(roundedX) >> 16;
			}
			right = left;
			return true;
		}
		if (step == 0u) {
			return false;
		}
		int lower = (y - y0) * int(FIXED_ONE) - int(fy + FIXED_HALF);
		int upper = lower + int(FIXED_ONE) - 1;
		if (upper < 0) {
			return false;
		}
		uint first = lower <= 0 ? 0u : (uint(lower) + step - 1u) / step;
		uint last = min(count - 1u, uint(upper) / step);
		if (first > last) {
			return false;
		}
		int origin = int(roundedX) >> 16;
		left = mode == 7u ? origin - int(last) : origin + int(first);
		right = mode == 7u ? origin - int(first) : origin + int(last);
		return true;
	}
	bool legacyCoverage(uint command, uint x, uint y)
	{
		uint first = commandWord(command, 0u), count = commandWord(command, 1u);
		bool line = (commandWord(command, 8u) & LINE) != 0u;
		int left = 2147483647, right = -2147483647;
		for (uint index = 0u; index < count; index++) {
			int start, end;
			if (legacyRow(first + index, int(y), line, start, end)) {
				left = min(left, start);
				right = max(right, end);
			}
		}
		return int(x) >= left && int(x) <= right;
	}
	bool coverage(uint command, uint x, uint y, thread float &depth)
	{
		if (x < commandWord(command, 14u) || x >= commandWord(command, 15u) ||
			y < commandWord(command, 16u) || y >= commandWord(command, 17u)) {
			return false;
		}
		if ((commandWord(command, 8u) & LEGACY) != 0u) {
			depth = 1.0f;
			return legacyCoverage(command, x, y);
		}
		uint first = commandWord(command, 0u);
		uint count = commandWord(command, 1u);
		if ((commandWord(command, 8u) & LINE) != 0u) {
			float x0 = edgeFloat(first, 0u), y0 = edgeFloat(first, 1u);
			float x1 = edgeFloat(first + 1u, 0u), y1 = edgeFloat(first + 1u, 1u);
			float dx = x1 - x0, dy = y1 - y0;
			depth = 1.0f;
			if (dx == 0.0f && dy == 0.0f) {
				return float(x) == x0 && float(y) == y0;
			}
			if (abs(dx) >= abs(dy)) {
				if (float(x) < min(x0, x1) || float(x) > max(x0, x1)) {
					return false;
				}
				return int(y) == int(floor(y0 + dy * ((float(x) - x0) / dx) + 0.5f));
			}
			if (float(y) < min(y0, y1) || float(y) > max(y0, y1)) {
				return false;
			}
			return int(x) == int(floor(x0 + dx * ((float(y) - y0) / dy) + 0.5f));
		}
		float px = float(x) + 0.5f, py = float(y) + 0.5f;
		float left = -3.402823e38f, right = 3.402823e38f, leftZ = 0.0f, rightZ = 0.0f;
		uint crossings = 0u;
		for (uint index = 0u; index < count; index++) {
			uint edge = first + index;
			if (int(y) < int(edgeWord(edge, 5u)) || int(y) >= int(edgeWord(edge, 6u))) {
				continue;
			}
			float distance = py - edgeFloat(edge, 1u);
			float position = edgeFloat(edge, 0u) + distance * edgeFloat(edge, 3u);
			float inverseZ = edgeFloat(edge, 2u) + distance * edgeFloat(edge, 4u);
			if (position <= px) {
				crossings++;
				if (position >= left) {
					left = position;
					leftZ = inverseZ;
				}
			} else if (position < right) {
				right = position;
				rightZ = inverseZ;
			}
		}
		if ((crossings & 1u) == 0u || !(left < right) || left <= -3.402822e38f ||
			right >= 3.402822e38f) {
			return false;
		}
		if (commandWord(command, 10u) != 0u) {
			depth = commandFloat(command, 13u) + float(x) * commandFloat(command, 11u) +
					float(y) * commandFloat(command, 12u);
		} else {
			depth = leftZ + (px - left) * (rightZ - leftZ) / (right - left);
		}
		return depth > 0.0f && !isnan(depth) && !isinf(depth);
	}
	bool paint(uint command, uint x, uint y, thread uint &color)
	{
		color = commandWord(command, 4u) & 255u;
		uint mode = commandWord(command, 7u);
		if (mode == 0u) {
			return true;
		}
		uint bit = ((y & 1u) == 0u ? 8u : 0u) + 7u - (x & 7u);
		bool set = (commandWord(command, 6u) & (1u << bit)) != 0u;
		if (mode == PAINT_ALTERNATE && set) {
			color = commandWord(command, 5u) & 255u;
		}
		return mode != PAINT_PATTERN || set;
	}
	float groundDepth(uint x, uint y)
	{
		return float(x) * as_type<float>(scene[15]) + float(y) * as_type<float>(scene[16]) +
			   as_type<float>(scene[17]);
	}
	bool sampleCommand(uint command, uint x, uint y, float inputDepth, thread float &depth,
					   thread uint &family, thread uint &color)
	{
		uint flags = commandWord(command, 8u), selected;
		if (!paint(command, x, y, selected)) {
			return false;
		}
		if ((flags & BACKGROUND) != 0u) {
			color = selected;
			return true;
		}
		uint candidateFamily = commandWord(command, 9u);
		float candidate = inputDepth * ((flags & DECAL) != 0u ? DECAL_SCALE : 1.0f);
		if (candidateFamily == 0u || !(candidate > 0.0f)) {
			return false;
		}
		if ((flags & GROUND_CLIP) != 0u && scene[14] != 0u && family == 0u &&
			candidate * (1.0f + EPSILON) < groundDepth(x, y)) {
			return false;
		}
		if ((flags & DECAL) != 0u && candidateFamily == family) {
			color = selected;
			return true;
		}
		if (family != 0u && candidate + EPSILON * depth < depth) {
			return false;
		}
		color = selected;
		depth = candidate;
		family = candidateFamily;
		return true;
	}
	void run(uint pixel)
	{
		uint width = scene[0], height = scene[1];
		if (pixel >= width * height) {
			return;
		}
		uint x = pixel % width, y = pixel / width;
		if (x < scene[2] || x >= scene[3] || y < scene[4] || y >= scene[5]) {
			return;
		}
		uint tile = (y / TILE_SIZE) * scene[12] + x / TILE_SIZE;
		uint tileAddress = scene[10] + tile * 2u;
		uint first = scene[11] + scene[tileAddress];
		uint count = scene[tileAddress + 1u];
		uint outputAddress = pixel * OUTPUT_WORDS;
		float depth = as_type<float>(image[outputAddress + 1u]);
		uint family = image[outputAddress + 2u], color = 0u;
		bool touched = false;
		if (scene[6] == 1u) {
			// The last covered sample wins painter order. Hidden commands
			// need no edge traversal once that sample has been found.
			for (uint index = count; index != 0u; index--) {
				uint command = scene[first + index - 1u];
				float ignored;
				uint selected;
				if (coverage(command, x, y, ignored) && paint(command, x, y, selected)) {
					color = selected;
					touched = true;
					break;
				}
			}
		} else {
			bool incremental = scene[18] != 0u;
			uint winner = NO_COMMAND;
			float winnerDepth = 0.0f;
			uint winnerFamily = 0u, winnerFlags = 0u;
			// Resolve opaque support first, then solid detail layers. This mirrors
			// the software visibility partition before sampling masked materials.
			for (uint pass = 0u; pass < 2u; pass++) {
				for (uint index = 0u; index < count; index++) {
					uint command = scene[first + index];
					uint flags = commandWord(command, 8u);
					bool detail = (flags & (DECAL | GROUND_CLIP)) != 0u;
					if (commandWord(command, 7u) == PAINT_PATTERN || detail != (pass == 1u)) {
						continue;
					}
					float candidate;
					if (!coverage(command, x, y, candidate)) {
						continue;
					}
					if (incremental) {
						touched = sampleCommand(command, x, y, candidate, depth, family, color) ||
								  touched;
						continue;
					}
					uint candidateFamily = commandWord(command, 9u);
					bool supported = winner != NO_COMMAND && (flags & DECAL) != 0u &&
									 candidateFamily != 0u && candidateFamily == winnerFamily;
					float testDepth =
						(flags & BACKGROUND) != 0u
							? 0.0f
							: candidate * ((flags & DECAL) != 0u ? DECAL_SCALE : 1.0f);
					float oldDepth =
						winner == NO_COMMAND
							? -3.402823e38f
							: winnerDepth * ((winnerFlags & DECAL) != 0u ? DECAL_SCALE : 1.0f);
					if (scene[14] != 0u && (flags & GROUND_CLIP) != 0u &&
						(winner == NO_COMMAND || (winnerFlags & BACKGROUND) != 0u)) {
						oldDepth = groundDepth(x, y);
					}
					if (supported || testDepth * (1.0f + EPSILON) >= oldDepth) {
						if (!supported) {
							winnerDepth = (flags & BACKGROUND) != 0u ? 0.0f : candidate;
						}
						winner = command;
						winnerFlags = flags;
						winnerFamily = (flags & BACKGROUND) != 0u ? 0u : candidateFamily;
					}
				}
			}
			if (!incremental && winner != NO_COMMAND) {
				uint selected;
				if (paint(winner, x, y, selected)) {
					color = selected;
					touched = true;
					depth = winnerDepth;
					family = winnerFamily;
				}
			}
			for (uint index = 0u; index < count; index++) {
				uint command = scene[first + index];
				if (commandWord(command, 7u) != PAINT_PATTERN) {
					continue;
				}
				float candidate;
				if (coverage(command, x, y, candidate)) {
					touched =
						sampleCommand(command, x, y, candidate, depth, family, color) || touched;
				}
			}
		}
		if (touched) {
			image[outputAddress] = color | TOUCHED;
		}
		image[outputAddress + 1u] = as_type<uint>(depth);
		image[outputAddress + 2u] = family;
	}
};

kernel void gpu_raster(device const uint *scene [[buffer(0)]], device uint *image [[buffer(1)]],
					   uint pixel [[thread_position_in_grid]])
{
	RasterScene raster = {scene, image};
	raster.run(pixel);
}
