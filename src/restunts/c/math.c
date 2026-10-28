#include "externs.h"
#include "legacy.h"
#include "math.h"
#ifdef RESTUNTS_SDL3
#include "extended_geometry.h"
#endif
#include "math_internal.h"
#include "fatal.h"
#include "track_collision.h"
#include "wheel_transform.h"
#include "projection.h"

legacy_s16 sign_word(legacy_s16 value)
{
	legacy_s16 signed_value = LEGACY_S16_FROM_BITS(value);
	if (signed_value < 0) {
		return -1;
	}
	return signed_value != 0;
}

legacy_s32 absolute_long(legacy_s32 value)
{
	legacy_u32 bits = (legacy_u32)value;
	if ((bits & LEGACY_U32_SIGN_BIT) != 0) {
		bits = (legacy_u32)(0UL - bits);
	}
	return LEGACY_S32_FROM_BITS(bits);
}

#define VECTOR_DIRECTION_NEGATIVE_Y 30
#define VECTOR_DIRECTION_POSITIVE_Y 31
#define VECTOR_DIRECTION_POSITIVE_Y_OFFSET 15
#define VECTOR_DIRECTION_SCALE_SHIFT 4U
#define VECTOR_DIRECTION_INDEX_SHIFT 10U
#define PROJECTION_COORD_LIMIT 32000
#define PROJECTION_INVALID_COORD_BITS LEGACY_U16_SIGN_BIT
#define PROJECTION_DEPTH_COMPARE_SHIFT 15U
#define MULTIPLY_ROUND_BIT ((legacy_u32)LEGACY_U16_SIGN_BIT)
#define MULTIPLY_ROUND_SHIFT (LEGACY_WORD_BITS - 1U)
#define NORMAL_INNER_PRODUCT_SCALE 8192L

/*
 * Scratch matrices used only by mat_rot_zxy().  Keeping them here makes the
 * routine self-contained instead of obtaining private temporary storage from
 * dseg.asm.  They remain static rather than automatic because callers use the
 * returned pointer until the next mat_rot_zxy() call.
 */
static struct MATRIX math_mat_z_rot;
static struct MATRIX math_mat_x_rot;
static struct MATRIX math_mat_y_rot;
static struct MATRIX math_mat_rot_temp;
static legacy_u16 math_mat_y_rot_angle;

legacy_s16 sintab[] = {
	0,	   101,	  201,	 302,	402,   503,	  603,	 704,	804,   904,	  1005,	 1105,	1205,
	1306,  1406,  1506,	 1606,	1706,  1806,  1906,	 2006,	2105,  2205,  2305,	 2404,	2503,
	2603,  2702,  2801,	 2900,	2999,  3098,  3196,	 3295,	3393,  3492,  3590,	 3688,	3786,
	3883,  3981,  4078,	 4176,	4273,  4370,  4467,	 4563,	4660,  4756,  4852,	 4948,	5044,
	5139,  5235,  5330,	 5425,	5520,  5614,  5708,	 5803,	5897,  5990,  6084,	 6177,	6270,
	6363,  6455,  6547,	 6639,	6731,  6823,  6914,	 7005,	7096,  7186,  7276,	 7366,	7456,
	7545,  7635,  7723,	 7812,	7900,  7988,  8076,	 8163,	8250,  8337,  8423,	 8509,	8595,
	8680,  8765,  8850,	 8935,	9019,  9102,  9186,	 9269,	9352,  9434,  9516,	 9598,	9679,
	9760,  9841,  9921,	 10001, 10080, 10159, 10238, 10316, 10394, 10471, 10549, 10625, 10702,
	10778, 10853, 10928, 11003, 11077, 11151, 11224, 11297, 11370, 11442, 11514, 11585, 11656,
	11727, 11797, 11866, 11935, 12004, 12072, 12140, 12207, 12274, 12340, 12406, 12472, 12537,
	12601, 12665, 12729, 12792, 12854, 12916, 12978, 13039, 13100, 13160, 13219, 13279, 13337,
	13395, 13453, 13510, 13567, 13623, 13678, 13733, 13788, 13842, 13896, 13949, 14001, 14053,
	14104, 14155, 14206, 14256, 14305, 14354, 14402, 14449, 14497, 14543, 14589, 14635, 14680,
	14724, 14768, 14811, 14854, 14896, 14937, 14978, 15019, 15059, 15098, 15137, 15175, 15213,
	15250, 15286, 15322, 15357, 15392, 15426, 15460, 15493, 15525, 15557, 15588, 15619, 15649,
	15679, 15707, 15736, 15763, 15791, 15817, 15843, 15868, 15893, 15917, 15941, 15964, 15986,
	16008, 16029, 16049, 16069, 16088, 16107, 16125, 16143, 16160, 16176, 16192, 16207, 16221,
	16235, 16248, 16261, 16273, 16284, 16295, 16305, 16315, 16324, 16332, 16340, 16347, 16353,
	16359, 16364, 16369, 16373, 16376, 16379, 16381, 16383, 16384, 16384};

extern legacy_u8 atantable[];

legacy_s16 sin_fast(legacy_u16 s)
{
	legacy_u8 c = s & ANGLE_QUARTER_MASK;
	switch ((s >> 8) & 3) {
		case 0:
			return sintab[c];
		case 1:
			return sintab[ANGLE_QUARTER_TURN - c];
		case 2:
			return -sintab[c];
		case 3:
			return -sintab[ANGLE_QUARTER_TURN - c];
	}

	return 0;
}

legacy_s16 cos_fast(legacy_u16 s)
{
	return sin_fast(LEGACY_U16_WRAP_ADD(s, ANGLE_QUARTER_TURN));
}

static legacy_s16 polar_angle_quadrant(legacy_u16 flag, legacy_s16 result)
{
	switch (flag) {
		case 0:
			return result;
		case 1:
			return -result + ANGLE_QUARTER_TURN;
		case 2:
			return -result + ANGLE_HALF_TURN;
		case 3:
			return result + ANGLE_QUARTER_TURN;
		case 4:
			return -result;
		case 5:
			return result - ANGLE_QUARTER_TURN;
		case 6:
			return result - ANGLE_HALF_TURN;
		case 7:
			return -(result + ANGLE_QUARTER_TURN);
	}

	return 0;
}

legacy_s16 polarAngle(legacy_s16 z, legacy_s16 y)
{

	legacy_u16 flag = 0;

	if (z < 0) {
		flag |= 4;
		z = LEGACY_S16_WRAP_NEGATE(z);
	}

	if (y < 0) {
		flag |= 2;
		y = LEGACY_S16_WRAP_NEGATE(y);
	}

	legacy_s16 result;
	if (z == y) {
		/* The legacy callers treat a zero-length direction as angle zero. */
		if (z == 0) {
			return 0;
		}
		result = ANGLE_EIGHTH_TURN;
	} else {
		if (z > y) {
			legacy_s16 coordinate_swap = z;
			z = y;
			y = coordinate_swap;
			flag |= 1;
		}
		legacy_u32 index =
			LEGACY_U32_DIV_OR_ZERO(LEGACY_U32_SHL((legacy_u16)z, 16U), (legacy_u16)y);
		if ((index & ANGLE_QUARTER_MASK) >= ANGLE_EIGHTH_TURN) {
			index += ANGLE_QUARTER_TURN;
		}
		result = atantable[index >> 8];
	}

	return polar_angle_quadrant(flag, result);
}

legacy_s16 polarRadius2D(legacy_s16 z, legacy_s16 y)
{
	legacy_s32 result = polarAngle(z, y);

	if (result < 0) {
		result = -result;
	}

	if (result >= ANGLE_QUARTER_TURN) {
		result = -(result - ANGLE_HALF_TURN);
	}

	if (result <= ANGLE_EIGHTH_TURN) {
		result = cos_fast(result);
		if (y < 0) {
			y = LEGACY_S16_WRAP_NEGATE(y);
		}
		return LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_U32_DIV_OR_ZERO(
			LEGACY_U32_SHL((legacy_u16)y, 14U), (legacy_u16)result));
	} else {
		result = sin_fast(result);
		if (z < 0) {
			z = LEGACY_S16_WRAP_NEGATE(z);
		}
		return LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_U32_DIV_OR_ZERO(
			LEGACY_U32_SHL((legacy_u16)z, 14U), (legacy_u16)result));
	}
}

/* Arithmetic shift right by one: the sign bit is kept as x86 SAR does. */
legacy_u16 sar1_word(legacy_u16 value)
{
	return LEGACY_U16_SAR(value, 1U);
}

legacy_s16 absolute_word(legacy_s16 value)
{
	return value < 0 ? LEGACY_S16_WRAP_NEGATE(value) : value;
}

/* World coordinates are kept as 26.6 fixed point longs; the engine works on
 * the whole-unit word. */
legacy_s16 position_to_word(legacy_s32 position)
{
	return LEGACY_S16_FROM_BITS((legacy_u16)LEGACY_S32_SAR(position, 6U));
}

legacy_s16 polarRadius3D(struct VECTOR *vec)
{
	return polarRadius2D(polarRadius2D(vec->x, vec->y), vec->z);
}

#ifndef RESTUNTS_HEADLESS

legacy_u16 rect_compare_point(struct POINT2D *pt)
{
	legacy_s8 flag;
	if (pt->py < select_rect_rc.top) {
		flag = 1;
	} else if (pt->py > select_rect_rc.bottom) {
		flag = 2;
	} else {
		flag = 0;
	}

	if (pt->px < select_rect_rc.left) {
		flag |= 4;
	} else if (pt->px > select_rect_rc.right) {
		flag |= 8;
	}
	return flag;
}
#endif

static legacy_s16 matrix_scaled_product(legacy_s16 left, legacy_s16 right)
{
	legacy_s32 product = LEGACY_S32_WRAP_MUL((legacy_s32)left, (legacy_s32)right);
	legacy_u32 scaled_bits = LEGACY_U32_SHL((legacy_u32)product, MATH_PRODUCT_SCALE_SHIFT);
	return LEGACY_S16_FROM_BITS((legacy_u16)(scaled_bits >> LEGACY_WORD_BITS));
}

static legacy_s16 matrix_row_product(legacy_s16 row_x, legacy_s16 row_y, legacy_s16 row_z,
									 struct VECTOR *invec)
{
	legacy_s16 result;

	if (row_x != 0 && invec->x != 0) {
		result = matrix_scaled_product(row_x, invec->x);
	} else {
		result = 0;
	}

	if (row_y != 0 && invec->y != 0) {
		result = LEGACY_S16_WRAP_ADD(result, matrix_scaled_product(row_y, invec->y));
	}

	if (row_z != 0 && invec->z != 0) {
		result = LEGACY_S16_WRAP_ADD(result, matrix_scaled_product(row_z, invec->z));
	}

	return result;
}

void mat_mul_vector(struct VECTOR *invec, struct MATRIX *mat, struct VECTOR *outvec)
{
	outvec->x = matrix_row_product(mat->m._11, mat->m._12, mat->m._13, invec);
	outvec->y = matrix_row_product(mat->m._21, mat->m._22, mat->m._23, invec);
	outvec->z = matrix_row_product(mat->m._31, mat->m._32, mat->m._33, invec);
}

void mat_mul_vector2(struct VECTOR *invec, struct MATRIX far *mat, struct VECTOR *outvec)
{
	struct MATRIX local_matrix = *mat;

	mat_mul_vector(invec, &local_matrix, outvec);
}

void mat_multiply(struct MATRIX *rmat, struct MATRIX *lmat, struct MATRIX *outmat)
{
	legacy_s16 *rmatvals = rmat->vals;
	legacy_s16 *lmatvals = lmat->vals;
	legacy_s16 *outmatvals = outmat->vals;

	legacy_s16 counter = MATRIX_ELEMENT_COUNT;
	while (counter > 0) {
		if (rmatvals[0] != 0 && lmatvals[0] != 0) {
			outmatvals[0] = matrix_scaled_product(rmatvals[0], lmatvals[0]);
		} else {
			outmatvals[0] = 0;
		}

		if (rmatvals[1] != 0 && lmatvals[3] != 0) {
			outmatvals[0] =
				LEGACY_S16_WRAP_ADD(outmatvals[0], matrix_scaled_product(rmatvals[1], lmatvals[3]));
		}

		if (rmatvals[2] != 0 && lmatvals[6] != 0) {
			outmatvals[0] =
				LEGACY_S16_WRAP_ADD(outmatvals[0], matrix_scaled_product(rmatvals[2], lmatvals[6]));
		}

		outmatvals++;
		if (counter != 7 && counter != 4) {
			lmatvals++;
		} else {
			lmatvals -= 2;
			rmatvals += 3;
		}
		counter = LEGACY_S16_WRAP_SUB(counter, 1);
	}
}

void mat_invert(struct MATRIX *inmat, struct MATRIX *outmat)
{
	if (inmat == outmat) {
		legacy_s16 element_swap = outmat->m._21;
		outmat->m._21 = outmat->m._12;
		outmat->m._12 = element_swap;

		element_swap = outmat->m._31;
		outmat->m._31 = outmat->m._13;
		outmat->m._13 = element_swap;

		element_swap = outmat->m._32;
		outmat->m._32 = outmat->m._23;
		outmat->m._23 = element_swap;
	} else {
		outmat->m._11 = inmat->m._11;
		outmat->m._12 = inmat->m._21;
		outmat->m._13 = inmat->m._31;

		outmat->m._21 = inmat->m._12;
		outmat->m._22 = inmat->m._22;
		outmat->m._23 = inmat->m._32;

		outmat->m._31 = inmat->m._13;
		outmat->m._32 = inmat->m._23;
		outmat->m._33 = inmat->m._33;
	}
}

void mat_rot_x(struct MATRIX *outmat, legacy_s16 angle)
{
	legacy_s16 c = cos_fast(angle);
	legacy_s16 s = sin_fast(angle);
	outmat->m._11 = TRIG_FIXED_ONE;
	outmat->m._21 = 0;
	outmat->m._31 = 0;
	outmat->m._12 = 0;
	outmat->m._22 = c;
	outmat->m._32 = s;
	outmat->m._13 = 0;
	outmat->m._23 = -s;
	outmat->m._33 = c;
}

void mat_rot_y(struct MATRIX *outmat, legacy_s16 angle)
{
	legacy_s16 c = cos_fast(angle);
	legacy_s16 s = sin_fast(angle);
	outmat->m._11 = c;
	outmat->m._21 = 0;
	outmat->m._31 = -s;
	outmat->m._12 = 0;
	outmat->m._22 = TRIG_FIXED_ONE;
	outmat->m._32 = 0;
	outmat->m._13 = s;
	outmat->m._23 = 0;
	outmat->m._33 = c;
}

void mat_rot_z(struct MATRIX *outmat, legacy_s16 angle)
{
	legacy_s16 c = cos_fast(angle);
	legacy_s16 s = sin_fast(angle);
	outmat->m._11 = c;
	outmat->m._21 = s;
	outmat->m._31 = 0;
	outmat->m._12 = -s;
	outmat->m._22 = c;
	outmat->m._32 = 0;
	outmat->m._13 = 0;
	outmat->m._23 = 0;
	outmat->m._33 = TRIG_FIXED_ONE;
}

// mat_rot_zxy was originally optimized, using pre-calced y-matrices and only
// multiplying the non-zero axes. currently not optimized except for the y cache:
//
// Checked against asmorig/seg006.asm:2293 and the results are identical, for
// three reasons worth writing down so the rewrite is not re-examined:
//
//  - the shortcut matrices are not constants. mat_y0/mat_y100/mat_y200/
//    mat_y300 are all zero in dseg and get filled by mat_rot_y itself in
//    init_polyinfo (shape3d.c:2672-2675), so they hold exactly what this
//    code recomputes.
//  - multiplying by the identity is exact here: mat_multiply forms
//    (a * b) >> 14 and the identity entry is 16384, so (v * 16384) >> 14
//    is v with nothing lost. Building all three axes and both products when
//    the original would have skipped an axis therefore cannot drift.
//  - the scratch state is private. mat_y_rot, mat_y_rot_angle, mat_z_rot,
//    mat_x_rot and mat_rot_temp are referenced from nowhere else in the
//    program, so the extra writes are invisible.
//
// What does differ is which of them the returned pointer points at: the
// original returns mat_y0 / the y matrix / mat_x_rot / mat_rot_temp /
// mat_z_rot depending on which axes were live. The contents are the same and
// no caller keeps the pointer across another call.

struct MATRIX *mat_rot_zxy(legacy_s16 z, legacy_s16 x, legacy_s16 y, legacy_s16 rotation_order)
{
	mat_rot_z(&math_mat_z_rot, z);
	mat_rot_x(&math_mat_x_rot, x);

	// y rotation matrix cache
	/*if (mat_y_rot_angle != y) {
		mat_rot_y(&mat_y_rot, y);
		mat_y_rot_angle = y;
	}*/
	math_mat_y_rot_angle = y; // dont forget this!!
	mat_rot_y(&math_mat_y_rot, y);

	if ((rotation_order & MATRIX_ROTATION_ORDER_MASK) == MATRIX_ROTATION_ORDER_YXZ) {
		mat_multiply(&math_mat_y_rot, &math_mat_x_rot, &math_mat_rot_temp);
		mat_multiply(&math_mat_rot_temp, &math_mat_z_rot, &math_mat_x_rot);
		return &math_mat_x_rot;
	} else {
		mat_multiply(&math_mat_z_rot, &math_mat_x_rot, &math_mat_rot_temp);
		mat_multiply(&math_mat_rot_temp, &math_mat_y_rot, &math_mat_z_rot);
		return &math_mat_z_rot;
	}
}

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

static legacy_u16 math_word_magnitude(legacy_s16 value)
{
	if (value < 0) {
		return (legacy_u16)(0U - (legacy_u16)value);
	}
	return (legacy_u16)value;
}

legacy_s16 vector_direction_sector(struct VECTOR *vec)
{
	legacy_s32 vertical_magnitude = (legacy_s32)math_word_magnitude(vec->y);

	// The original widens the 16-bit radius with an explicit zero high word
	// (mov [bp+var_4], ax / mov [bp+var_2], 0), not with a sign extension.
	legacy_s32 horizontal_radius =
		(legacy_u16)polarRadius2D(LEGACY_S16_FROM_BITS(math_word_magnitude(vec->x)),
								  LEGACY_S16_FROM_BITS(math_word_magnitude(vec->z)));

	if (direction_sector_sine != direction_sector_cosine) {
		//fatal_error("direction_sector_sine != direction_sector_cosine - not observed");
		vertical_magnitude = vertical_magnitude * direction_sector_sine;
		horizontal_radius = horizontal_radius * direction_sector_cosine;
	}

	legacy_s16 vertical_sector;
	if (horizontal_radius >= vertical_magnitude) {
		vertical_sector = 0;
	} else {
		vertical_sector = 1;
	}

	if (vec->y < 0) {
		if (vertical_sector != 0) {
			return VECTOR_DIRECTION_NEGATIVE_Y;
		}
	} else if (vec->y > 0) {
		if (vertical_sector != 0) {
			return VECTOR_DIRECTION_POSITIVE_Y;
		}
	}

	legacy_s16 result;
	if (vec->y > 0) {
		result = VECTOR_DIRECTION_POSITIVE_Y_OFFSET;
	} else {
		result = 0;
	}

	legacy_s32 angle = -polarAngle(vec->z, -vec->x);
	if (angle < 0) {
		angle += ANGLE_FULL_TURN;
	}

	legacy_s32 scaled_angle =
		LEGACY_S32_WRAP_SUB(LEGACY_S32_SHL(angle, VECTOR_DIRECTION_SCALE_SHIFT), angle);
	result = LEGACY_S16_WRAP_ADD(
		result, (legacy_s16)LEGACY_S32_SAR(scaled_angle, VECTOR_DIRECTION_INDEX_SHIFT));

	return result;
}

// All ten of these are `dw` in dseg and are declared unsigned in shape3d.c,
// which is also how set_projection produces them. math.c used to declare this
// subset as int, so projection_focal_length_x/10 - the only ones that can grow past
// 32767 - reached the projection multiply as negative values.
//
// The two roles are not the same, and the casts at the use sites say which is
// which: 9 and 10 are the operands of `mul`, so they stay unsigned there,
// while 5 and 8 are added with `add ax, .. / jo`, whose overflow test reads
// both operands as signed words.

// Each `add ax, projectiondataN` in the original is followed by `jo`, and on
// overflow the sum is replaced by the rail it ran past: 32000 when the true
// sum was too positive (the wrapped word comes back negative, `or ax,ax / jl`)
// and -32000 when it was too negative.
static legacy_s16 saturate_projection(legacy_s32 sum)
{
	if (sum > (legacy_s32)LEGACY_S16_MAX) {
		return PROJECTION_COORD_LIMIT;
	}
	if (sum < -(legacy_s32)LEGACY_U16_SIGN_BIT) {
		return -PROJECTION_COORD_LIMIT;
	}
	return (legacy_s16)sum;
}

// NEG in the original wraps in AX and is followed by unsigned MUL. In
// particular, -(-32768) remains the sign-bit pattern and is used as the
// magnitude 32768.
static legacy_u16 projection_magnitude(legacy_s16 value)
{
	return (legacy_u16)(0U - (legacy_u16)value);
}

void vector_to_point(struct VECTOR *vec, struct POINT2D *outpt)
{
	// bx in the original: (proj >> 16) << 1 plus the top bit of the low word,
	// which is exactly proj >> 15 kept in 16 bits. `cmp cx, bx / jle` then
	// weighs it against z as a signed word.
	legacy_s16 comp;

	if (vec->z <= 0) {
		outpt->px = LEGACY_S16_FROM_BITS(PROJECTION_INVALID_COORD_BITS);
		outpt->py = LEGACY_S16_FROM_BITS(PROJECTION_INVALID_COORD_BITS);
		return;
	}

	legacy_u32 proj;
	if (vec->x < 0) {
		proj = (legacy_u32)projection_magnitude(vec->x) * (legacy_u16)projection_focal_length_x;
		comp = (legacy_s16)(proj >> PROJECTION_DEPTH_COMPARE_SHIFT);

		if (vec->z > comp) {
			outpt->px =
				saturate_projection(-(legacy_s32)LEGACY_U32_DIV_OR_ZERO(proj, (legacy_u16)vec->z) +
									(legacy_s16)projection_center_x);
		} else {
			outpt->px = -PROJECTION_COORD_LIMIT;
		}
	} else {
		proj = (legacy_u32)(legacy_u16)vec->x * (legacy_u16)projection_focal_length_x;
		comp = (legacy_s16)(proj >> PROJECTION_DEPTH_COMPARE_SHIFT);

		if (vec->z > comp) {
			outpt->px =
				saturate_projection((legacy_s32)LEGACY_U32_DIV_OR_ZERO(proj, (legacy_u16)vec->z) +
									(legacy_s16)projection_center_x);
		} else {
			outpt->px = PROJECTION_COORD_LIMIT;
		}
	}

	if (vec->y < 0) {
		proj = (legacy_u32)projection_magnitude(vec->y) * (legacy_u16)projection_focal_length_y;
		comp = (legacy_s16)(proj >> PROJECTION_DEPTH_COMPARE_SHIFT);

		if (vec->z > comp) {
			outpt->py =
				saturate_projection((legacy_s32)LEGACY_U32_DIV_OR_ZERO(proj, (legacy_u16)vec->z) +
									(legacy_s16)projection_center_y);
		} else {
			outpt->py = PROJECTION_COORD_LIMIT;
		}
	} else {
		proj = (legacy_u32)(legacy_u16)vec->y * (legacy_u16)projection_focal_length_y;
		comp = (legacy_s16)(proj >> PROJECTION_DEPTH_COMPARE_SHIFT);

		if (vec->z > comp) {
			outpt->py =
				saturate_projection(-(legacy_s32)LEGACY_U32_DIV_OR_ZERO(proj, (legacy_u16)vec->z) +
									(legacy_s16)projection_center_y);
		} else {
			outpt->py = -PROJECTION_COORD_LIMIT;
		}
	}
}
#endif

static legacy_s16 vector_interpolate_axis(legacy_s16 first, legacy_s16 second, legacy_s16 factor,
										  legacy_s16 divisor)
{
	legacy_s32 product =
		LEGACY_S32_WRAP_MUL((legacy_s32)LEGACY_S16_WRAP_SUB(first, second), (legacy_s32)factor);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(product, (legacy_s32)divisor);
	return LEGACY_S16_WRAP_ADD(LEGACY_S16_FROM_BITS((legacy_u16)quotient), second);
}

void vector_interpolate_at_z(struct VECTOR *first, struct VECTOR *second, struct VECTOR *result,
							 legacy_s16 depth)
{
	result->z = depth;

	legacy_s16 depth_offset = LEGACY_S16_WRAP_SUB(result->z, second->z);
	legacy_s16 depth_span = LEGACY_S16_WRAP_SUB(first->z, second->z);
	if (depth_span < 0) {
		/* The original uses a 16-bit logical SHR for both values. */
		depth_offset = LEGACY_S16_FROM_BITS((legacy_u16)depth_offset >> 1);
		depth_span = LEGACY_S16_FROM_BITS((legacy_u16)depth_span >> 1);
	}

	result->x = vector_interpolate_axis(first->x, second->x, depth_offset, depth_span);
	result->y = vector_interpolate_axis(first->y, second->y, depth_offset, depth_span);
}

extern legacy_u8 vector_saved_z_low;
extern legacy_u8 vector_saved_z_high;

void vector_interpolate_at_saved_z(struct VECTOR *vec1, struct VECTOR *vec2, struct VECTOR *outvec)
{
	legacy_u16 interpolation_z =
		(legacy_u16)(vector_saved_z_low | LEGACY_U16_SHL(vector_saved_z_high, 8U));
	vector_interpolate_at_z(vec1, vec2, outvec, LEGACY_S16_FROM_BITS(interpolation_z));
}

legacy_s16 multiply_and_scale(legacy_s16 left, legacy_s16 right)
{
	legacy_s32 product = LEGACY_S32_WRAP_MUL((legacy_s32)left, (legacy_s32)right);
	legacy_u32 scaled_bits = LEGACY_U32_SHL((legacy_u32)product, MATH_PRODUCT_SCALE_SHIFT);
	legacy_u16 high_word = (legacy_u16)(scaled_bits >> LEGACY_WORD_BITS);
	legacy_u16 round_up = (legacy_u16)((scaled_bits & MULTIPLY_ROUND_BIT) >> MULTIPLY_ROUND_SHIFT);
	return LEGACY_S16_FROM_BITS(LEGACY_U16_WRAP_ADD(high_word, round_up));
}

legacy_s16 vec_normalInnerProduct(legacy_s16 x, legacy_s16 y, legacy_s16 z,
								  struct VECTOR far *normal)
{
	legacy_s32 x_product = LEGACY_S32_WRAP_MUL((legacy_s32)normal->x, (legacy_s32)x);
	legacy_s32 y_product = LEGACY_S32_WRAP_MUL((legacy_s32)normal->y, (legacy_s32)y);
	legacy_s32 z_product = LEGACY_S32_WRAP_MUL((legacy_s32)normal->z, (legacy_s32)z);
	legacy_s32 sum = LEGACY_S32_WRAP_ADD(LEGACY_S32_WRAP_ADD(x_product, z_product), y_product);
	legacy_s32 quotient = LEGACY_S32_DIV_OR_ZERO(sum, NORMAL_INNER_PRODUCT_SCALE);
	return LEGACY_S16_FROM_BITS((legacy_u16)quotient);
}

legacy_s16 plane_signed_distance(legacy_s16 plane_index, legacy_s16 x, legacy_s16 y, legacy_s16 z)
{
	struct PLANE far *plane;
	if (plane_index == planindex) {
		plane = current_planptr;
	} else {
		plane = &planptr[plane_index];
	}

	struct VECTOR world_origin;
	world_origin.y = plane->plane_origin.y + terrainHeight;
	struct VECTOR relative_position;
	relative_position.y = y - world_origin.y;
	if (plane_index < 4) {
		// NOTE: what is this
		return relative_position.y;
	}
	world_origin.x = plane->plane_origin.x + elem_xCenter;
	world_origin.z = plane->plane_origin.z + elem_zCenter;
	relative_position.x = x - world_origin.x;
	relative_position.z = z - world_origin.z;
	return vec_normalInnerProduct(relative_position.x, relative_position.y, relative_position.z,
								  &plane->plane_normal);
}

extern struct MATRIX wheel_heading_rotation;
extern legacy_s16 cached_plane_heading;
extern struct MATRIX plane_heading_rotation;
extern legacy_s16 cached_wheel_heading;

void transform_wheel_travel_to_world(void)
{
	struct VECTOR world_direction;
	struct PLANE far *plane;
	struct VECTOR plane_direction;
	struct MATRIX inverse_plane_rotation;
	struct MATRIX plane_rotation;
	if (planindex_copy != -1) {
#ifdef RESTUNTS_SDL3
		plane = planindex_copy >= EXTENDED_GEOMETRY_PLANE_INDEX ? current_planptr
																: &planptr[planindex_copy];
#else
		plane = &planptr[planindex_copy];
#endif
		legacy_s16 heading;
		if (plane->plane_xy == car_initial_pitch && plane->plane_yz == car_initial_roll) {
			heading = car_initial_yaw;
		} else {
			mat_mul_vector(&wheel_forward_travel, &car_to_world_rotation, &world_direction);
			plane_rotation = plane->plane_rotation;
			mat_invert(&plane_rotation, &inverse_plane_rotation);
			mat_mul_vector(&world_direction, &inverse_plane_rotation, &plane_direction);
			heading = polarAngle(-plane_direction.x, plane_direction.z);
		}

		heading += wheel_heading_offset;
		if (heading == 0) {
			mat_mul_vector2(&wheel_forward_travel, &plane->plane_rotation, &wheel_world_travel);
			return;
		}
		if (cached_plane_heading != heading) {
			mat_rot_y(&plane_heading_rotation, -heading);
			cached_plane_heading = heading;
		}
		mat_mul_vector(&wheel_forward_travel, &plane_heading_rotation, &plane_direction);
		mat_mul_vector2(&plane_direction, &plane->plane_rotation, &wheel_world_travel);
		return;
	}

	if (wheel_heading_offset == 0) {
		mat_mul_vector(&wheel_forward_travel, &car_to_world_rotation, &wheel_world_travel);
		return;
	}
	if (wheel_heading_offset != cached_wheel_heading) {
		mat_rot_y(&wheel_heading_rotation, -wheel_heading_offset);
		cached_wheel_heading = wheel_heading_offset;
	}
	mat_mul_vector(&wheel_forward_travel, &wheel_heading_rotation, &plane_direction);
	mat_mul_vector(&plane_direction, &car_to_world_rotation, &wheel_world_travel);
}
