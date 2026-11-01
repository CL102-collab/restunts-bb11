#include "../externs.h"
#include "../legacy.h"
#include "math.h"
#include "math_internal.h"
#include "../fatal.h"
#include "../track/track_collision.h"
#include "../wheel_transform.h"
#include "projection.h"

#ifndef RESTUNTS_HEADLESS

void rect_adjust_from_point(struct POINT2D *pt, struct RECTANGLE *rc)
{
	if (rc->left > pt->px) {
		rc->left = pt->px;
	}

	legacy_s16 exclusive_bound = pt->px + 1;
	if (rc->right < exclusive_bound) {
		rc->right = exclusive_bound;
	}

	if (rc->top > pt->py) {
		rc->top = pt->py;
	}

	exclusive_bound = pt->py + 1;
	if (rc->bottom < exclusive_bound) {
		rc->bottom = exclusive_bound;
	}
}

void rect_union(struct RECTANGLE *r1, struct RECTANGLE *r2, struct RECTANGLE *outrc)
{
	if (r1->left <= r2->left) {
		outrc->left = r1->left;
	} else {
		outrc->left = r2->left;
	}

	if (r1->right >= r2->right) {
		outrc->right = r1->right;
	} else {
		outrc->right = r2->right;
	}

	if (r1->top <= r2->top) {
		outrc->top = r1->top;
	} else {
		outrc->top = r2->top;
	}

	if (r1->bottom >= r2->bottom) {
		outrc->bottom = r1->bottom;
	} else {
		outrc->bottom = r2->bottom;
	}

	if (video_x_alignment == 1) {
		return;
	}

	// Unreachable. video_x_alignment is written exactly once in the whole
	// program - init_main sets it to 1 (asmorig/seg031.asm:237,
	// restunts.c:1260) - and video_x_alignment_mask alongside it to 65535.
	// The suppressed tail is `right = (right + video_x_alignment - 1) &
	// video_x_alignment_mask`, which at those values is the identity anyway, so
	// the port loses nothing by not carrying it.
	fatal_error((const legacy_s8 *)"rect_union: unexpected code path");
}

legacy_s16 rect_intersect(struct RECTANGLE *r1, struct RECTANGLE *r2)
{
	if (r1->right < r1->left) {
		return 1;
	}
	if (r2->right <= r1->left) {
		return 1;
	}
	if (r1->right <= r2->left) {
		return 1;
	}
	if (r1->top >= r2->bottom) {
		return 1;
	}
	if (r1->bottom <= r2->top) {
		return 1;
	}

	if (r1->left < r2->left) {
		r1->left = r2->left;
	}

	if (r1->right > r2->right) {
		r1->right = r2->right;
	}

	if (r1->top < r2->top) {
		r1->top = r2->top;
	}

	if (r1->bottom > r2->bottom) {
		r1->bottom = r2->bottom;
	}
	return 0;
}

legacy_s16 rect_is_inside(struct RECTANGLE *r1, struct RECTANGLE *r2)
{
	if (r1->right > r2->right) {
		return 0;
	}

	if (r1->left < r2->left) {
		return 0;
	}

	if (r1->top < r2->top) {
		return 0;
	}

	if (r1->bottom > r2->bottom) {
		return 0;
	}

	return 1;
}

legacy_s16 rect_is_overlapping(struct RECTANGLE *r1, struct RECTANGLE *r2)
{
	if (r1->right <= r2->left) {
		return 0;
	}

	if (r2->right <= r1->left) {
		return 0;
	}

	if (r1->top >= r2->bottom) {
		return 0;
	}

	if (r1->bottom <= r2->top) {
		return 0;
	}

	return 1;
}

legacy_s16 rect_is_adjacent(struct RECTANGLE *r1, struct RECTANGLE *r2)
{
	if (r1->bottom == r2->top || r1->top == r2->bottom) {
		if (r1->left == r2->left && r1->right == r2->right) {
			return 1;
		}
		return 0;
	}

	if (r1->right == r2->left || r2->right == r1->left) {
		if (r1->top == r2->top && r1->bottom == r2->bottom) {
			return 1;
		}
	}
	return 0;
}

static void rectlist_remove_at(legacy_s8 *length, struct RECTANGLE *rectangles, legacy_s16 index)
{
	while (((*length) - 1) > index) {
		rectangles[index] = rectangles[index + 1];
		index++;
	}
	(*length)--;
}

static void rectlist_split_overlap(struct RECTANGLE *rect, struct RECTANGLE *existing_rectangle,
								   struct RECTANGLE *merged_rectangle,
								   struct RECTANGLE *upper_remainder,
								   struct RECTANGLE *lower_remainder,
								   legacy_s16 *has_upper_remainder, legacy_s16 *has_lower_remainder)
{
	*merged_rectangle = *existing_rectangle;
	if (existing_rectangle->top >= rect->top) {
		if (rect->top < existing_rectangle->top) {
			*upper_remainder = *rect;
			upper_remainder->bottom = existing_rectangle->top;
			*has_upper_remainder = 1;
		} else {
			*has_upper_remainder = 0;
		}
	} else {
		*upper_remainder = *existing_rectangle;
		upper_remainder->bottom = rect->top;
		merged_rectangle->top = rect->top;
		*has_upper_remainder = 1;
	}

	if (existing_rectangle->bottom <= rect->bottom) {
		if (rect->bottom > existing_rectangle->bottom) {
			*lower_remainder = *rect;
			lower_remainder->top = existing_rectangle->bottom;
			*has_lower_remainder = 1;
		} else {
			*has_lower_remainder = 0;
		}
	} else {
		*lower_remainder = *existing_rectangle;
		lower_remainder->top = rect->bottom;
		merged_rectangle->bottom = rect->bottom;
		*has_lower_remainder = 1;
	}

	if (rect->left <= existing_rectangle->left) {
		merged_rectangle->left = rect->left;
	} else {
		merged_rectangle->left = existing_rectangle->left;
	}

	if (rect->right >= existing_rectangle->right) {
		merged_rectangle->right = rect->right;
	} else {
		merged_rectangle->right = existing_rectangle->right;
	}
}

void rectlist_add_rect(legacy_s8 *rectangle_count, struct RECTANGLE *rectangles,
					   struct RECTANGLE *rect)
{
	if (video_x_alignment != 1) {
		// Unreachable, for the same reason as the one in rect_union above:
		// video_x_alignment is only ever set to 1, in init_main.
		fatal_error((const legacy_s8 *)"rectlist_add_rect: unexpected code path");
	}

	legacy_s16 has_upper_remainder;
	struct RECTANGLE merged_rectangle;
	struct RECTANGLE upper_remainder;
	struct RECTANGLE lower_remainder;
	struct RECTANGLE *existing_rectangle;
	legacy_s16 has_lower_remainder;
	for (legacy_s16 rectangle_index = 0; rectangle_index < *rectangle_count; rectangle_index++) {
		existing_rectangle = &rectangles[rectangle_index];
		if (rect_is_overlapping(rect, existing_rectangle) == 0) {
			continue;
		}
		if (rect_is_inside(rect, existing_rectangle) != 0) {
			return;
		}

		if (rect_is_inside(existing_rectangle, rect) != 0) {
			rectlist_remove_at(rectangle_count, rectangles, rectangle_index);
			continue;
		}

		rectlist_split_overlap(rect, existing_rectangle, &merged_rectangle, &upper_remainder,
							   &lower_remainder, &has_upper_remainder, &has_lower_remainder);
		rectlist_remove_at(rectangle_count, rectangles, rectangle_index);
		if (has_upper_remainder != 0) {
			rectlist_add_rect(rectangle_count, rectangles, &upper_remainder);
		}

		rectlist_add_rect(rectangle_count, rectangles, &merged_rectangle);
		if (has_lower_remainder != 0) {
			rectlist_add_rect(rectangle_count, rectangles, &lower_remainder);
			return;
		}
		return;
	}

	for (legacy_s16 rectangle_index = 0; rectangle_index < *rectangle_count; rectangle_index++) {
		existing_rectangle = &rectangles[rectangle_index];

		if (rect_is_adjacent(existing_rectangle, rect) == 0) {
			continue;
		}
		rect_union(existing_rectangle, rect, &merged_rectangle);

		rectlist_remove_at(rectangle_count, rectangles, rectangle_index);
		rectlist_add_rect(rectangle_count, rectangles, &merged_rectangle);
		return;
	}

	rectangles[*rectangle_count] = *rect;
	(*rectangle_count)++;
}

void rectlist_add_rects(legacy_s8 source_count, legacy_s8 *source_flags,
						struct RECTANGLE *first_rectangles, struct RECTANGLE *second_rectangles,
						struct RECTANGLE *clip_rectangle, legacy_s8 *output_count,
						struct RECTANGLE *output_rectangles)
{
	/*
	return ported_rect_clip_combined_(
		source_count, source_flags, first_rectangles, second_rectangles, clip_rectangle,
		output_count, output_rectangles);
	*/
	struct RECTANGLE *second_rectangle;
	struct RECTANGLE *first_rectangle;
	struct RECTANGLE *selected_rectangle;
	struct RECTANGLE clipped_rectangle;
	struct RECTANGLE merged_rectangle;
	for (legacy_s16 source_index = 0; source_index < source_count; source_index++) {
		legacy_s16 flags = source_flags[source_index];
		if ((flags & 1) != 0) {
			first_rectangle = &first_rectangles[source_index];
		}

		if ((flags & 2) != 0) {
			second_rectangle = &second_rectangles[source_index];
		}

		legacy_s16 has_rectangle;
		if (((flags & 1) == 0) || first_rectangle->right <= first_rectangle->left) {
			if (((flags & 2) == 0) || second_rectangle->right <= second_rectangle->left) {
				has_rectangle = 0;
			} else {
				selected_rectangle = second_rectangle;
				has_rectangle = 1;
			}
		} else if ((flags & 2) == 0) {
			selected_rectangle = first_rectangle;
			has_rectangle = 1;
		} else if (second_rectangle->right <= second_rectangle->left) {
			selected_rectangle = first_rectangle;
			has_rectangle = 1;
		} else {
			rect_union(first_rectangle, second_rectangle, &merged_rectangle);
			selected_rectangle = &merged_rectangle;
			has_rectangle = 1;
		}

		if (has_rectangle != 0) {
			clipped_rectangle = *selected_rectangle;
			if (rect_intersect(&clipped_rectangle, clip_rectangle) == 0) {
				rectlist_add_rect(output_count, output_rectangles, &clipped_rectangle);
			}
		}
	}
}

void rect_array_sort_by_top(legacy_s8 rectangle_count, struct RECTANGLE *rectangles,
							legacy_s16 *sorted_indices)
{
	//return ported_rect_array_indexed_op_(rectangle_count, rectangles, sorted_indices);
	legacy_s16 sort_keys[256];
	if (rectangle_count > 1) {
		for (legacy_s16 rectangle_index = 0; rectangle_index < rectangle_count; rectangle_index++) {
			sort_keys[rectangle_index] = -rectangles[rectangle_index].top;
			sorted_indices[rectangle_index] = rectangle_index;
		}
		heapsort_by_order(rectangle_count, sort_keys, sorted_indices);
	} else {
		sorted_indices[0] = 0;
	}
}

#endif
