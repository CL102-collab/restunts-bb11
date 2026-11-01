#ifndef RESTUNTS_MATH_H
#define RESTUNTS_MATH_H

#include "../legacy.h"

enum MATRIX_ROTATION_ORDER { MATRIX_ROTATION_ORDER_ZXY = 0, MATRIX_ROTATION_ORDER_YXZ = 1 };

#define MATRIX_ROTATION_ORDER_MASK 1U

#define ANGLE_EIGHTH_TURN 128
#define ANGLE_QUARTER_TURN 256
#define ANGLE_HALF_TURN 512
#define ANGLE_THREE_QUARTER_TURN 768
#define ANGLE_FULL_TURN 1024
#define ANGLE_QUARTER_MASK 255U
#define ANGLE_MASK 1023U
#define TRIG_FIXED_ONE 16384
#define MATH_PRODUCT_SCALE_SHIFT 2U
#define MATRIX_ELEMENT_COUNT 9U

#pragma pack(push, 1)

struct RECTANGLE {
	legacy_s16 left, right;
	legacy_s16 top, bottom;
	//int x1, y1;
	//int x2, y2;
};

struct VECTOR {
	legacy_s16 x, y, z;
};

struct VECTORLONG {
	legacy_s32 lx, ly, lz;
};

struct POINT2D {
	legacy_s16 px, py;
};

struct MATRIX {
	union {
		legacy_s16 vals[MATRIX_ELEMENT_COUNT];
		struct {
			legacy_s16 _11, _21, _31;
			legacy_s16 _12, _22, _32;
			legacy_s16 _13, _23, _33;
		} m;
	};
};

struct PLANE {
	legacy_s16 plane_yz;
	legacy_s16 plane_xy;
	struct VECTOR plane_origin;
	struct VECTOR plane_normal;
	struct MATRIX plane_rotation;
};

#pragma pack(pop)

typedef char legacy_rectangle_must_be_8_bytes[(sizeof(struct RECTANGLE) == 8) ? 1 : -1];
typedef char legacy_vector_must_be_6_bytes[(sizeof(struct VECTOR) == 6) ? 1 : -1];
typedef char legacy_vectorlong_must_be_12_bytes[(sizeof(struct VECTORLONG) == 12) ? 1 : -1];
typedef char legacy_point2d_must_be_4_bytes[(sizeof(struct POINT2D) == 4) ? 1 : -1];
typedef char legacy_matrix_must_be_18_bytes[(sizeof(struct MATRIX) == 18) ? 1 : -1];
typedef char legacy_plane_must_be_34_bytes[(sizeof(struct PLANE) == 34) ? 1 : -1];

legacy_s16 sin_fast(legacy_u16 s);
legacy_s16 cos_fast(legacy_u16 s);

legacy_s16 polarAngle(legacy_s16 z, legacy_s16 y);
legacy_s16 polarRadius2D(legacy_s16 z, legacy_s16 y);
legacy_s16 polarRadius3D(struct VECTOR *vec);

legacy_u16 rect_compare_point(struct POINT2D *pt);

void mat_mul_vector(struct VECTOR *invec, struct MATRIX *mat, struct VECTOR *outvec);
void mat_mul_vector2(struct VECTOR *invec, struct MATRIX far *mat, struct VECTOR *outvec);
void mat_multiply(struct MATRIX *rmat, struct MATRIX *lmat, struct MATRIX *outmat);
void mat_invert(struct MATRIX *inmat, struct MATRIX *outmat);
void mat_rot_x(struct MATRIX *outmat, legacy_s16 angle);
void mat_rot_y(struct MATRIX *outmat, legacy_s16 angle);
void mat_rot_z(struct MATRIX *outmat, legacy_s16 angle);
struct MATRIX *mat_rot_zxy(legacy_s16 z, legacy_s16 x, legacy_s16 y, legacy_s16 rotation_order);

void rect_adjust_from_point(struct POINT2D *pt, struct RECTANGLE *rc);
void rectlist_add_rects(legacy_s8 rectcount, legacy_s8 *indices, struct RECTANGLE *first,
						struct RECTANGLE *second, struct RECTANGLE *clip, legacy_s8 *output_length,
						struct RECTANGLE *output);
void rect_array_sort_by_top(legacy_s8 length, struct RECTANGLE *rects, legacy_s16 *indices);

legacy_s16 vector_direction_sector(struct VECTOR *vec);
void vector_to_point(struct VECTOR *vec, struct POINT2D *outpt);
void vector_interpolate_at_z(struct VECTOR *vec1, struct VECTOR *vec2, struct VECTOR *outvec,
							 legacy_s16 i);

legacy_s16 multiply_and_scale(legacy_s16 left, legacy_s16 right);
legacy_s16 absolute_word(legacy_s16 value);
legacy_u16 sar1_word(legacy_u16 value);
legacy_s16 position_to_word(legacy_s32 position);

void rect_union(struct RECTANGLE *r1, struct RECTANGLE *r2, struct RECTANGLE *outrc);
legacy_s16 rect_intersect(struct RECTANGLE *r1, struct RECTANGLE *r2);

void heapsort_by_order(legacy_s16 count, legacy_s16 *values, legacy_s16 *order);

#endif
