#ifndef RESTUNTS_TRACK_TYPES_H
#define RESTUNTS_TRACK_TYPES_H

#include "../math/math.h"

struct SHAPE3D;

#pragma pack(push, 1)

struct TRKOBJINFO {
	// How many shapeInfo pieces compose the element. Arbitrary for the first piece, 0 for the
	// following ones.
	legacy_s8 si_noOfBlocks;
	legacy_s8 si_entryPoint; // Connectivity of the track element regarding tiles.
	legacy_s8 si_exitPoint;
	legacy_s8 si_entryType; // Connectivity of the track element regarding element types.
	legacy_s8 si_exitType;
	legacy_s8 route_point_count;  // Number of route points in this element path.
	legacy_s16 route_orientation; // Rotation shared by route, camera and roadside-sign placement.
	struct VECTOR *route_vectors; // Forward route edge vectors.
	legacy_s8 reverse_path_offset_low; // Low byte of the legacy reverse-route vector offset.
	legacy_s8 reverse_path_offset_high;
	legacy_s8 roadside_sign_type;
	legacy_s8 opponent_speed_code;
};

struct TRACK_WALL {
	legacy_s16 orientation;
	legacy_s16 x;
	legacy_s16 z;
};

struct TRACKOBJECT {
	struct TRKOBJINFO *ss_trkObjInfoPtr; // offset (0003B770)
	legacy_s16 ss_rotY;					 // Horizontal orientation of the element.
	struct SHAPE3D *ss_shapePtr;		 // offset (0003B770)
	struct SHAPE3D *ss_loShapePtr;		 // offset (0003B770)
	legacy_u8 ss_ssOvelay;				 // Renders additional sceneShapes over the current one.
	legacy_s8 ss_surfaceType;			 // Paintjob. FF will induce alternating paintjobs.
	legacy_s8
		ss_ignoreZBias; // Appears to be Z-bias override flag, mostly used for roads and corners.
	// 0 = one-tile, 1 = two-tile vertical, 2 = two-tile horizontal, 3 = four-tile.
	legacy_s8 ss_multiTileFlag;
	legacy_s8 ss_physicalModel;	   // sets the physical model in build_track_object
	legacy_s8 reserved_scene_byte; // always zero.
};

#pragma pack(pop)

typedef char legacy_track_wall_must_be_6_bytes[(sizeof(struct TRACK_WALL) == 6) ? 1 : -1];

#if defined(RESTUNTS_DOS16)
typedef char legacy_trkobjinfo_must_be_14_bytes[(sizeof(struct TRKOBJINFO) == 14) ? 1 : -1];
typedef char legacy_trackobject_must_be_14_bytes[(sizeof(struct TRACKOBJECT) == 14) ? 1 : -1];

#endif

#endif
