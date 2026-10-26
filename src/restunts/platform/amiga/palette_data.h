#ifndef RESTUNTS_AMIGA_PALETTE_DATA_H
#define RESTUNTS_AMIGA_PALETTE_DATA_H

#include "../../c/legacy.h"

/* Numeric color facts from the original Amiga release. These are ECS 0xRGB
 * register values, not artwork. Source hashes, decoded offsets, extraction
 * checks, material-table provenance, and mapping confidence are recorded in
 * tests/fixtures/amiga-palette-reference.json. The original racing display
 * uses SDTITL !def in the world, STDB<car> !pal below/above the windshield,
 * and SDGAME !pal for replay controls; each racing band uses its first 16. */

struct AMIGA_ORIGINAL_PALETTE {
	const legacy_char *resource;
	const legacy_char *chunk;
	legacy_u16 colors[32];
};

static const struct AMIGA_ORIGINAL_PALETTE amiga_original_palettes[] = {
	/* disk1/OPP1LOSE.psh, decoded palette offset 0x3a. */
	{"OPP1LOSE",
	 "!pal",
	 {
		 0x000, 0x00a, 0xff0, 0x444, 0xa00, 0xa0a, 0x853, 0xaaa, 0x555, 0x494, 0x321,
		 0xb85, 0x742, 0x777, 0xff5, 0xfda, 0xfbb, 0xc96, 0xfdd, 0xed0, 0x272, 0xf99,
		 0x6b6, 0x5a5, 0xca8, 0xfbf, 0xfdf, 0xa41, 0xf77, 0x7d7, 0xfca, 0xfff,
	 }},
	/* disk1/OPP1WIN.psh, decoded palette offset 0x3a. */
	{"OPP1WIN",
	 "!pal",
	 {
		 0x000, 0x00a, 0xff0, 0x853, 0xa00, 0xa0a, 0xfdc, 0xaaa, 0x555, 0xb85, 0x666,
		 0xc96, 0x444, 0x742, 0xff5, 0xeca, 0x594, 0xc97, 0xfbb, 0x777, 0xed0, 0xfdf,
		 0xf99, 0xdb9, 0x383, 0xfca, 0xea7, 0x530, 0xfbf, 0x999, 0xfb9, 0xfff,
	 }},
	/* disk1/OPP2LOSE.psh, decoded palette offset 0x52. */
	{"OPP2LOSE",
	 "!pal",
	 {
		 0x000, 0x00a, 0xb2f, 0x222, 0xa00, 0xa0a, 0x305, 0xaaa, 0x555, 0x742, 0x963,
		 0xb85, 0xe85, 0x531, 0xff5, 0x606, 0xea8, 0xc63, 0xfca, 0xc96, 0xb52, 0xe97,
		 0x931, 0x80c, 0xfdc, 0x508, 0xca8, 0x90e, 0x700, 0xfb9, 0x007, 0xfff,
	 }},
	/* disk1/OPP2WIN.psh, decoded palette offset 0x52. */
	{"OPP2WIN",
	 "!pal",
	 {
		 0x000, 0x00a, 0x853, 0xb85, 0xa00, 0xa0a, 0x742, 0xaaa, 0x555, 0xe97, 0x531,
		 0x305, 0xe85, 0xfdc, 0xff5, 0xeca, 0xc96, 0xfed, 0xfc9, 0xea8, 0xfda, 0xfca,
		 0xfc8, 0xca8, 0xdb9, 0xfb6, 0xa61, 0xd74, 0xc62, 0xfa5, 0x930, 0xfff,
	 }},
	/* disk1/OPP3LOSE.psh, decoded palette offset 0x42. */
	{"OPP3LOSE",
	 "!pal",
	 {
		 0x000, 0x00a, 0x7b0, 0xddd, 0xa00, 0xa0a, 0xb85, 0xaaa, 0x555, 0x333, 0x742,
		 0x963, 0xfb9, 0xc96, 0xff5, 0xa41, 0xfbb, 0xe97, 0xfca, 0xf99, 0x666, 0xd84,
		 0xea8, 0xc97, 0x888, 0x594, 0xdb9, 0xf77, 0x9e0, 0x505, 0x999, 0xfff,
	 }},
	/* disk1/OPP3WIN.psh, decoded palette offset 0x5a. */
	{"OPP3WIN",
	 "!pal",
	 {
		 0x000, 0x00a, 0x7b0, 0x333, 0xa00, 0xa0a, 0xb85, 0xaaa, 0x555, 0xddd, 0xfbb,
		 0x963, 0xc96, 0x742, 0xff5, 0x999, 0xe96, 0xfb9, 0xf99, 0xfcb, 0xca8, 0xea8,
		 0x666, 0xfca, 0x5a5, 0x888, 0xd84, 0xf77, 0xdb9, 0xc63, 0x9e0, 0xfff,
	 }},
	/* disk1/opp4lose.psh, decoded palette offset 0x52. */
	{"OPP4LOSE",
	 "!pal",
	 {
		 0x000, 0x00a, 0xf77, 0x222, 0xf33, 0xa0a, 0xb85, 0xaaa, 0x555, 0xfdc, 0x963,
		 0xf55, 0x852, 0xf99, 0xff5, 0xca8, 0xc96, 0xe85, 0xfbb, 0xfda, 0xea8, 0x642,
		 0xfdf, 0xfc9, 0xfed, 0xfca, 0x930, 0xc62, 0x777, 0xdb9, 0xfb6, 0xfff,
	 }},
	/* disk1/OPP4WIN.psh, decoded palette offset 0x62. */
	{"OPP4WIN",
	 "!pal",
	 {
		 0x000, 0x00a, 0xf77, 0x222, 0xa00, 0xa0a, 0x853, 0xaaa, 0x555, 0xf99, 0xfbb,
		 0x742, 0xf9f, 0xa74, 0xff5, 0xfff, 0xf55, 0xea8, 0xc62, 0x444, 0xfbf, 0xa41,
		 0xe85, 0x777, 0xf7f, 0xc96, 0x930, 0xfdf, 0xfca, 0xfdd, 0x500, 0x888,
	 }},
	/* disk1/OPP5LOSE.psh, decoded palette offset 0x5a. */
	{"OPP5LOSE",
	 "!pal",
	 {
		 0x000, 0x00a, 0xfbf, 0xb85, 0xa00, 0xa0a, 0x963, 0xaaa, 0x555, 0x400, 0x740,
		 0xc96, 0xe85, 0xda8, 0xff5, 0xfa5, 0xea7, 0xc71, 0xeb9, 0xfca, 0x930, 0xfc8,
		 0xc97, 0xa41, 0xfc9, 0xfb6, 0xeca, 0xf99, 0xfda, 0xfbb, 0xd74, 0xfff,
	 }},
	/* disk1/OPP5WIN.psh, decoded palette offset 0x62. */
	{"OPP5WIN",
	 "!pal",
	 {
		 0x000, 0x00a, 0xfbf, 0x400, 0xa00, 0xa0a, 0x740, 0xaaa, 0x555, 0x963, 0xb85,
		 0xe85, 0xc96, 0xea8, 0xff5, 0xd74, 0xc97, 0x930, 0xfca, 0xfda, 0xfc8, 0xc71,
		 0xdb9, 0x777, 0xfc9, 0xa41, 0xe97, 0xfb6, 0xeca, 0xfb9, 0xfdc, 0xfff,
	 }},
	/* disk1/OPP6LOSE.psh, decoded palette offset 0x62. */
	{"OPP6LOSE",
	 "!pal",
	 {
		 0x000, 0x00a, 0x0ee, 0x222, 0xa00, 0xa0a, 0x444, 0xaaa, 0x555, 0x500, 0x666,
		 0x305, 0xe85, 0xa74, 0xff5, 0xf92, 0x742, 0x963, 0x930, 0x777, 0xc62, 0xfb9,
		 0xa41, 0xfdc, 0x0bb, 0xfcb, 0xc96, 0x999, 0xd74, 0xea8, 0xe82, 0xfff,
	 }},
	/* disk1/opp6win.psh, decoded palette offset 0x62. */
	{"OPP6WIN",
	 "!pal",
	 {
		 0x000, 0x00a, 0x0ee, 0x400, 0xa00, 0xa0a, 0x853, 0xaaa, 0x555, 0x742, 0xa74,
		 0xd84, 0x004, 0x444, 0xff5, 0xca8, 0xea8, 0xe96, 0xc62, 0x700, 0x930, 0xed0,
		 0x777, 0xe82, 0xf92, 0xc96, 0xa50, 0xcc0, 0x0aa, 0xfb9, 0x407, 0xfff,
	 }},
	/* disk2/sdcred.psh, decoded palette offset 0x7a. */
	{"SDCRED",
	 "!pal",
	 {
		 0x000, 0x00a, 0x0a0, 0x0aa, 0xa00, 0xa0a, 0xa50, 0xaaa, 0x555, 0x55f, 0x5f5,
		 0x5ff, 0xf55, 0xf5f, 0xff5, 0xfff, 0xfff, 0xddd, 0xccc, 0xccc, 0xbbb, 0xaaa,
		 0x999, 0x888, 0x777, 0x777, 0x666, 0x555, 0x444, 0x333, 0x222, 0x222,
	 }},
	/* disk2/sdcsel.psh, decoded palette offset 0x32. */
	{"SDCSEL",
	 "!pal",
	 {
		 0x000, 0x00a, 0x0a0, 0x0aa, 0xa00, 0xa0a, 0xa50, 0xaaa, 0x555, 0x55f, 0x5f5,
		 0x5ff, 0xf55, 0xf5f, 0xff5, 0xfff, 0x666, 0xe00, 0xf22, 0xddd, 0xbbb, 0x00f,
		 0x00b, 0xccc, 0xccc, 0x00d, 0x007, 0x888, 0xc00, 0x500, 0x999, 0xfff,
	 }},
	/* disk2/sdgame.psh, decoded palette offset 0x132. */
	{"SDGAME",
	 "!pal",
	 {
		 0x000, 0x00a, 0x777, 0x55f, 0xa00, 0x888, 0x555, 0x594, 0xddd, 0xaaa, 0x777,
		 0x999, 0x7c6, 0x222, 0x262, 0xfff, 0x8e8, 0x666, 0xe00, 0xfff, 0xc00, 0xb00,
		 0xf00, 0x800, 0x333, 0xccc, 0x900, 0xbbb, 0x700, 0x222, 0xf22, 0x444,
	 }},
	/* disk2/sdmsel.psh, decoded palette offset 0x2a. */
	{"SDMSEL",
	 "!pal",
	 {
		 0x000, 0x00a, 0x470, 0x555, 0xa00, 0xa0a, 0x5bf, 0x580, 0x494, 0xe00, 0xcc0,
		 0xccc, 0x00f, 0x980, 0xff5, 0x777, 0x00e, 0xf92, 0x00b, 0xccc, 0xddd, 0xfff,
		 0x690, 0xf00, 0xc96, 0x7cf, 0x44f, 0x666, 0x009, 0xbbb, 0xdef, 0xfff,
	 }},
	/* disk1/sdosel.psh, decoded palette offset 0x6a. */
	{"SDOSEL",
	 "!pal",
	 {
		 0x000, 0x00a, 0xddd, 0x400, 0xa00, 0xc71, 0xccc, 0xaaa, 0x555, 0xa61, 0x00e,
		 0x740, 0xff0, 0x7b0, 0x951, 0x0ee, 0x555, 0xb2f, 0xfbf, 0x204, 0xe82, 0xf92,
		 0xf77, 0xa74, 0xd84, 0xb85, 0x800, 0x963, 0xea8, 0xc96, 0xe85, 0xfff,
	 }},
	/* disk2/sdtitl.psh, decoded palette offset 0x42. */
	{"SDTITL",
	 "!def",
	 {
		 0x000, 0x00a, 0x0a0, 0x0aa, 0xa00, 0xa0a, 0xa50, 0xaaa, 0x555, 0x55f, 0x5f5,
		 0x5ff, 0xf55, 0xf5f, 0xff5, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff,
		 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0xfff,
	 }},
	/* disk2/sdtitl.psh, decoded palette offset 0x96. */
	{"SDTITL",
	 "!pa0",
	 {
		 0x000, 0xfd5, 0xeb4, 0xda3, 0xd83, 0xc72, 0xb62, 0xa51, 0x941, 0x831, 0x720,
		 0x610, 0x510, 0x400, 0x999, 0xfff, 0xf00, 0xf00, 0xf00, 0xf00, 0xf00, 0xf00,
		 0xf00, 0xf00, 0xf00, 0xf00, 0xf00, 0xf00, 0xf00, 0xf00, 0xf00, 0xf10,
	 }},
	/* disk2/sdtitl.psh, decoded palette offset 0xea. */
	{"SDTITL",
	 "!pa1",
	 {
		 0x000, 0x00a, 0xff2, 0xc00, 0xa00, 0x222, 0xe00, 0x650, 0xed0, 0xf00, 0x555,
		 0xba0, 0x980, 0x333, 0x777, 0x800, 0xb00, 0x900, 0x700, 0x870, 0x540, 0xcc0,
		 0x888, 0x666, 0x500, 0xaaa, 0xccc, 0xfbb, 0xff7, 0xccc, 0x420, 0xfff,
	 }},
	/* disk2/STDBANSX.psh, decoded palette offset 0x52. */
	{"STDBANSX",
	 "!pal",
	 {
		 0x000, 0x00a, 0x555, 0x333, 0xa00, 0x222, 0x666, 0x222, 0x444, 0x777, 0x888,
		 0x555, 0x777, 0xfff, 0xccc, 0xfff, 0x999, 0xaaa, 0xf00, 0xbbb, 0xddd, 0x035,
		 0xccc, 0xb00, 0xfa3, 0xfb6, 0xfc8, 0xe82, 0xf22, 0x383, 0x151, 0x5a5,
	 }},
	/* disk2/stdbaudi.psh, decoded palette offset 0x52. */
	{"STDBAUDI",
	 "!pal",
	 {
		 0x000, 0x00a, 0x222, 0x333, 0xa00, 0x222, 0x444, 0x666, 0x777, 0x555, 0x555,
		 0x777, 0x888, 0xaaa, 0xccc, 0xfff, 0x0a0, 0x5bf, 0xa50, 0xbef, 0xdef, 0xddd,
		 0xccc, 0x0aa, 0xbbb, 0xa0a, 0x999, 0xaaa, 0xfff, 0x55f, 0x5f5, 0x5ff,
	 }},
	/* disk2/STDBCOUN.psh, decoded palette offset 0x52. */
	{"STDBCOUN",
	 "!pal",
	 {
		 0x000, 0x00a, 0x222, 0xfa3, 0xa00, 0xaaa, 0xbbb, 0x444, 0x999, 0xfc8, 0xc71,
		 0xfb6, 0x666, 0xe82, 0xa61, 0xfff, 0x888, 0x951, 0xccc, 0xfc9, 0x530, 0xff5,
		 0xf22, 0x740, 0xfda, 0xddd, 0xe00, 0xfff, 0xff2, 0xf55, 0xf77, 0xfbb,
	 }},
	/* disk2/STDBFGTO.psh, decoded palette offset 0x52. */
	{"STDBFGTO",
	 "!pal",
	 {
		 0x000, 0x00a, 0x555, 0x666, 0xa00, 0x222, 0x333, 0x444, 0x777, 0x888, 0x999,
		 0xf22, 0xf92, 0xbbb, 0xaaa, 0xfff, 0xccc, 0xddd, 0xc00, 0xdef, 0xe82, 0xc71,
		 0xb00, 0x035, 0xa61, 0xfc9, 0xe00, 0xfff, 0xf77, 0xfb6, 0x036, 0xfa3,
	 }},
	/* disk2/STDBJAGU.psh, decoded palette offset 0x52. */
	{"STDBJAGU",
	 "!pal",
	 {
		 0x000, 0x00a, 0x222, 0x333, 0xa00, 0x606, 0x404, 0x444, 0xcc0, 0x555, 0x777,
		 0x888, 0xed0, 0x666, 0xaaa, 0xfff, 0xfff, 0xbbb, 0x80c, 0xccc, 0x505, 0xddd,
		 0x870, 0x999, 0xb4f, 0xe00, 0x980, 0x540, 0x800, 0xb00, 0x407, 0x700,
	 }},
	/* disk2/STDBLANC.psh, decoded palette offset 0x52. */
	{"STDBLANC",
	 "!pal",
	 {
		 0x000, 0x00a, 0x222, 0xa61, 0xa00, 0x951, 0xc71, 0x530, 0xfa5, 0x740, 0xf92,
		 0x742, 0x852, 0xe82, 0xfa3, 0xfff, 0x420, 0x963, 0x853, 0xfb6, 0xc96, 0xfc8,
		 0x777, 0x642, 0xddd, 0x333, 0x888, 0x666, 0xa50, 0x444, 0xfdc, 0xfda,
	 }},
	/* disk2/STDBLM02.psh, decoded palette offset 0x52. */
	{"STDBLM02",
	 "!pal",
	 {
		 0x000, 0x00a, 0x222, 0x333, 0xa00, 0xc71, 0x555, 0x777, 0xa61, 0xccc, 0x444,
		 0x888, 0xddd, 0x666, 0xaaa, 0xfff, 0x951, 0xe82, 0x999, 0xfff, 0xf92, 0x740,
		 0xbbb, 0x420, 0x530, 0xf00, 0x900, 0xfb6, 0x594, 0x383, 0x7c6, 0xa50,
	 }},
	/* disk2/STDBP962.psh, decoded palette offset 0x52. */
	{"STDBP962",
	 "!pal",
	 {
		 0x000, 0x00a, 0x222, 0x555, 0xa00, 0xf22, 0x666, 0x777, 0x777, 0x888, 0xbbb,
		 0xf44, 0xfff, 0xaaa, 0xb00, 0xfff, 0x999, 0x700, 0xccc, 0xddd, 0xc00, 0x900,
		 0x800, 0x951, 0xf92, 0xf55, 0xfbb, 0xf77, 0x00a, 0x740, 0xccc, 0x530,
	 }},
	/* disk2/stdbpc04.psh, decoded palette offset 0x52. */
	{"STDBPC04",
	 "!pal",
	 {
		 0x000, 0x00a, 0x222, 0x444, 0xa00, 0x333, 0x555, 0x888, 0x666, 0xddd, 0x777,
		 0xccc, 0x999, 0xaaa, 0xfa3, 0xfff, 0xfff, 0xe00, 0xb00, 0xe82, 0x900, 0xbbb,
		 0x951, 0xc00, 0xfa5, 0xa61, 0xf00, 0xf22, 0x800, 0xc71, 0x0a0, 0xa0a,
	 }},
	/* disk2/stdbpmin.psh, decoded palette offset 0x52. */
	{"STDBPMIN",
	 "!pal",
	 {
		 0x000, 0x00a, 0x333, 0x222, 0xa00, 0x222, 0x666, 0x777, 0x444, 0x555, 0xff4,
		 0xba0, 0x870, 0xff2, 0x555, 0xfff, 0x777, 0xcc0, 0x980, 0xff9, 0xf00, 0xed0,
		 0x650, 0x888, 0xaaa, 0xbbb, 0xccc, 0x900, 0x999, 0x540, 0xccc, 0x5a5,
	 }},
	/* disk2/STDBVETT.psh, decoded palette offset 0xa2. */
	{"STDBVETT",
	 "!pal",
	 {
		 0x000, 0x00a, 0x222, 0x777, 0xa00, 0x555, 0x666, 0x333, 0x444, 0x888, 0x951,
		 0x999, 0xbbb, 0xf92, 0xff5, 0xfff, 0x530, 0xaaa, 0xddd, 0xc71, 0xccc, 0x740,
		 0xa61, 0x500, 0xfff, 0xe82, 0xf22, 0xfa5, 0xfc9, 0xfa3, 0x420, 0xdef,
	 }},
};

#define AMIGA_ORIGINAL_PALETTE_COUNT                                                               \
	(sizeof(amiga_original_palettes) / sizeof(amiga_original_palettes[0]))

/* Exact SDMAIN.PVS !pal bytes: VGA DAC components in the range 0..63. */
static const legacy_u8 amiga_dos_reference_palette[256][3] = {
	{0, 0, 0},	  {0, 0, 42},	{0, 42, 0},	  {0, 42, 42},	{42, 0, 0},	  {42, 0, 42},
	{42, 21, 0},  {42, 42, 42}, {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63},
	{63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63}, {63, 63, 63}, {55, 55, 55},
	{51, 51, 51}, {48, 48, 48}, {44, 44, 44}, {41, 41, 41}, {38, 38, 38}, {34, 34, 34},
	{31, 31, 31}, {28, 28, 28}, {24, 24, 24}, {21, 21, 21}, {18, 18, 18}, {14, 14, 14},
	{11, 11, 11}, {8, 8, 8},	{63, 54, 54}, {63, 46, 46}, {63, 39, 39}, {63, 31, 31},
	{63, 23, 23}, {63, 16, 16}, {63, 8, 8},	  {63, 0, 0},	{57, 0, 0},	  {51, 0, 0},
	{45, 0, 0},	  {39, 0, 0},	{33, 0, 0},	  {28, 0, 0},	{22, 0, 0},	  {16, 0, 0},
	{63, 58, 54}, {63, 55, 48}, {63, 53, 43}, {63, 50, 37}, {63, 48, 32}, {63, 46, 26},
	{63, 43, 21}, {63, 41, 15}, {63, 39, 10}, {56, 34, 8},	{49, 30, 7},  {42, 25, 5},
	{36, 21, 4},  {29, 16, 2},	{22, 12, 1},  {16, 8, 0},	{63, 63, 54}, {63, 63, 46},
	{63, 63, 39}, {63, 63, 31}, {63, 62, 23}, {63, 61, 16}, {63, 61, 8},  {63, 61, 0},
	{57, 54, 0},  {51, 48, 0},	{45, 41, 0},  {39, 35, 0},	{33, 29, 0},  {27, 22, 0},
	{21, 16, 0},  {16, 10, 0},	{62, 63, 54}, {61, 63, 46}, {58, 63, 39}, {56, 63, 31},
	{52, 63, 23}, {49, 63, 16}, {45, 63, 8},  {40, 63, 0},	{36, 57, 0},  {32, 51, 0},
	{29, 45, 0},  {24, 39, 0},	{20, 33, 0},  {17, 28, 0},	{13, 22, 0},  {10, 16, 0},
	{54, 63, 54}, {39, 63, 39}, {36, 59, 36}, {33, 56, 33}, {30, 52, 30}, {28, 49, 27},
	{25, 46, 25}, {22, 42, 22}, {20, 39, 19}, {17, 36, 16}, {14, 32, 13}, {11, 29, 11},
	{9, 26, 8},	  {6, 22, 5},	{3, 19, 2},	  {1, 16, 0},	{54, 63, 63}, {46, 63, 63},
	{39, 63, 63}, {31, 63, 62}, {23, 63, 63}, {16, 63, 63}, {8, 63, 63},  {0, 63, 63},
	{0, 57, 57},  {0, 51, 51},	{0, 45, 45},  {0, 39, 39},	{0, 33, 33},  {0, 28, 28},
	{0, 22, 22},  {0, 16, 16},	{54, 59, 63}, {46, 56, 63}, {39, 53, 63}, {31, 50, 63},
	{23, 47, 63}, {16, 44, 63}, {0, 39, 63},  {0, 35, 57},	{0, 32, 52},  {0, 29, 47},
	{0, 25, 42},  {0, 22, 36},	{0, 19, 31},  {0, 15, 26},	{0, 12, 21},  {0, 9, 16},
	{54, 54, 63}, {46, 47, 63}, {39, 39, 63}, {31, 32, 63}, {23, 24, 63}, {16, 16, 63},
	{0, 1, 63},	  {0, 0, 57},	{0, 0, 52},	  {0, 0, 47},	{0, 0, 42},	  {0, 0, 36},
	{0, 0, 31},	  {0, 0, 26},	{0, 0, 21},	  {0, 0, 16},	{60, 54, 63}, {57, 46, 63},
	{54, 39, 63}, {52, 31, 63}, {50, 23, 63}, {47, 16, 63}, {45, 8, 63},  {42, 0, 63},
	{38, 0, 57},  {32, 0, 51},	{29, 0, 45},  {24, 0, 39},	{20, 0, 33},  {17, 0, 28},
	{13, 0, 22},  {10, 0, 16},	{63, 54, 63}, {63, 46, 63}, {63, 39, 63}, {63, 31, 63},
	{63, 23, 63}, {63, 16, 63}, {63, 8, 63},  {56, 0, 57},	{51, 0, 51},  {46, 0, 46},
	{41, 0, 41},  {36, 0, 36},	{31, 0, 31},  {26, 0, 26},	{21, 0, 21},  {16, 0, 16},
	{63, 58, 55}, {60, 53, 49}, {57, 49, 43}, {54, 45, 38}, {51, 40, 32}, {48, 36, 27},
	{45, 32, 21}, {43, 28, 16}, {39, 25, 14}, {35, 23, 13}, {32, 21, 11}, {28, 19, 10},
	{24, 16, 8},  {21, 14, 7},	{17, 12, 5},  {14, 10, 4},	{63, 54, 51}, {62, 51, 46},
	{61, 48, 42}, {60, 45, 37}, {59, 42, 33}, {58, 39, 29}, {57, 37, 25}, {56, 35, 22},
	{54, 32, 19}, {53, 30, 16}, {50, 27, 13}, {48, 24, 11}, {45, 21, 9},  {42, 18, 7},
	{39, 15, 5},  {37, 12, 3},	{61, 48, 42}, {60, 47, 40}, {60, 46, 39}, {60, 45, 37},
	{59, 44, 36}, {59, 43, 34}, {59, 42, 33}, {58, 41, 32}, {58, 40, 30}, {58, 39, 29},
	{57, 38, 27}, {57, 37, 26}, {57, 37, 25}, {63, 39, 39}, {63, 37, 37}, {63, 36, 36},
	{63, 35, 35}, {63, 33, 33}, {63, 32, 32}, {63, 31, 31}, {54, 45, 38}, {52, 43, 35},
	{51, 41, 33}, {50, 39, 31}, {49, 37, 29}, {48, 36, 27}, {48, 48, 48}, {47, 47, 47},
	{46, 46, 46}, {45, 45, 45}, {44, 44, 44}, {63, 63, 63},
};

/* Each observed DOS index uses its most frequent original Amiga index in
 * equal-sized matching bitmap chunks. Other entries use nearest RGB; ties
 * choose the lowest index. Different original artwork cannot be reproduced
 * exactly by a single index map; see confidence/pair counts in the fixture. */
struct AMIGA_ORIGINAL_COLOR_MAP {
	const legacy_char *resource;
	legacy_u8 indexes[256];
};

static const struct AMIGA_ORIGINAL_COLOR_MAP amiga_original_color_maps[] = {
	/* 63228/64000 paired pixels agree; 87 observed DOS indexes. */
	{"SDMSEL",
	 {
		 0,	 1,	 2,	 6,	 4,	 5,	 13, 29, 3,	 26, 8,	 25, 17, 11, 14, 31, 21, 20, 19, 11, 29, 29,
		 15, 15, 15, 27, 27, 27, 3,	 3,	 3,	 3,	 20, 11, 24, 17, 23, 23, 23, 23, 9,	 9,	 4,	 4,
		 4,	 3,	 4,	 0,	 20, 20, 20, 17, 17, 24, 17, 17, 17, 17, 24, 9,	 13, 2,	 3,	 0,	 21, 20,
		 14, 14, 14, 14, 14, 10, 10, 10, 13, 13, 13, 2,	 3,	 3,	 21, 20, 20, 14, 14, 14, 10, 10,
		 10, 22, 22, 22, 7,	 2,	 2,	 3,	 20, 29, 29, 29, 22, 8,	 8,	 8,	 8,	 8,	 8,	 8,	 2,	 2,
		 2,	 0,	 30, 30, 25, 25, 25, 6,	 6,	 6,	 6,	 6,	 6,	 8,	 8,	 8,	 3,	 3,	 30, 30, 25, 25,
		 6,	 6,	 6,	 6,	 26, 26, 1,	 28, 28, 28, 28, 0,	 30, 11, 25, 26, 26, 26, 12, 16, 18, 18,
		 1,	 28, 28, 3,	 28, 0,	 30, 20, 11, 0,	 3,	 5,	 26, 5,	 5,	 18, 5,	 5,	 28, 28, 28, 0,
		 21, 20, 20, 11, 11, 5,	 5,	 5,	 5,	 5,	 5,	 5,	 5,	 3,	 3,	 3,	 20, 20, 19, 29, 24, 24,
		 13, 13, 13, 13, 2,	 3,	 3,	 3,	 2,	 0,	 20, 20, 11, 29, 24, 24, 24, 24, 24, 24, 24, 13,
		 13, 13, 4,	 4,	 11, 11, 11, 29, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
		 24, 24, 29, 24, 24, 24, 24, 24, 29, 29, 29, 29, 29, 21,
	 }},
	/* 36368/36368 paired pixels agree; 44 observed DOS indexes. */
	{"SDCSEL",
	 {
		 0,	 1,	 2,	 3,	 4,	 5,	 6,	 7,	 8,	 9,	 10, 11, 12, 13, 14, 15, 31, 19, 23, 24, 20, 7,
		 30, 27, 27, 16, 16, 16, 8,	 0,	 0,	 0,	 19, 23, 20, 12, 12, 12, 18, 18, 17, 28, 4,	 4,
		 29, 29, 29, 29, 19, 19, 19, 23, 14, 14, 14, 12, 12, 12, 6,	 6,	 6,	 6,	 29, 29, 15, 19,
		 14, 14, 14, 14, 14, 14, 14, 6,	 6,	 6,	 6,	 8,	 29, 29, 15, 19, 19, 14, 14, 14, 14, 10,
		 10, 10, 6,	 2,	 2,	 2,	 8,	 0,	 19, 20, 7,	 10, 10, 10, 27, 16, 16, 8,	 8,	 8,	 8,	 2,
		 0,	 0,	 15, 19, 11, 11, 11, 11, 11, 11, 3,	 3,	 3,	 3,	 3,	 3,	 8,	 26, 19, 19, 23, 11,
		 11, 11, 3,	 3,	 3,	 3,	 3,	 3,	 26, 26, 26, 26, 19, 23, 20, 9,	 9,	 9,	 21, 25, 25, 22,
		 1,	 26, 26, 0,	 0,	 26, 15, 19, 23, 13, 13, 13, 13, 5,	 5,	 5,	 5,	 5,	 26, 26, 26, 26,
		 15, 19, 13, 13, 13, 13, 13, 5,	 5,	 5,	 5,	 5,	 5,	 8,	 8,	 29, 19, 19, 23, 20, 7,	 30,
		 27, 6,	 6,	 6,	 6,	 8,	 8,	 8,	 29, 29, 19, 19, 23, 20, 20, 12, 12, 12, 12, 12, 6,	 6,
		 6,	 6,	 6,	 6,	 23, 23, 23, 20, 20, 20, 20, 7,	 7,	 12, 12, 12, 12, 20, 20, 12, 12, 12,
		 12, 12, 20, 7,	 7,	 7,	 30, 30, 20, 20, 20, 20, 7,	 15,
	 }},
	/* 100976/105200 paired pixels agree; 218 observed DOS indexes. */
	{"SDOSEL",
	 {
		 0,	 0,	 13, 7,	 4,	 22, 9,	 7,	 8,	 17, 13, 15, 22, 17, 12, 31, 31, 2,	 6,	 6,	 7,	 7,
		 7,	 7,	 8,	 8,	 8,	 8,	 8,	 3,	 3,	 3,	 28, 28, 28, 22, 22, 22, 22, 15, 24, 4,	 4,	 14,
		 26, 11, 3,	 3,	 28, 28, 28, 28, 28, 28, 21, 21, 21, 20, 5,	 9,	 14, 11, 3,	 3,	 2,	 2,
		 12, 12, 12, 12, 12, 12, 12, 12, 29, 27, 14, 11, 3,	 3,	 31, 28, 12, 12, 12, 12, 12, 13,
		 13, 13, 13, 13, 8,	 8,	 0,	 0,	 2,	 6,	 6,	 6,	 13, 13, 13, 13, 13, 13, 8,	 8,	 8,	 0,
		 0,	 0,	 31, 2,	 2,	 6,	 15, 15, 15, 15, 15, 15, 15, 1,	 8,	 8,	 8,	 19, 2,	 2,	 15, 15,
		 15, 15, 15, 15, 15, 1,	 1,	 8,	 1,	 19, 19, 19, 28, 2,	 17, 17, 17, 10, 10, 10, 10, 1,
		 1,	 1,	 8,	 19, 19, 19, 18, 18, 17, 17, 17, 17, 17, 17, 17, 17, 1,	 1,	 1,	 8,	 19, 19,
		 28, 18, 18, 18, 18, 17, 17, 17, 17, 17, 7,	 1,	 8,	 8,	 19, 3,	 28, 28, 28, 28, 29, 29,
		 25, 23, 27, 14, 11, 11, 11, 3,	 3,	 3,	 28, 28, 28, 28, 28, 28, 30, 30, 24, 24, 5,	 5,
		 9,	 14, 14, 14, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 30, 30, 28, 28, 22, 28, 22,
		 28, 22, 6,	 28, 29, 29, 29, 29, 6,	 6,	 6,	 7,	 7,	 27,
	 }},
	/* 52570/52576 dashboard pixels agree; first 16 car colors only. */
	{"STDBCOUN",
	 {
		 0,	 1,	 2,	 3,	 4,	 5, 6,	5,	7,	9,	10, 11, 12, 13, 9,	15, 15, 15, 6,	6,	6,	5,
		 8,	 8,	 8,	 12, 12, 7, 7,	2,	2,	2,	15, 9,	9,	3,	13, 13, 13, 13, 13, 4,	4,	4,
		 4,	 4,	 7,	 2,	 15, 9, 9,	9,	9,	11, 3,	3,	3,	13, 10, 14, 14, 7,	7,	2,	15, 15,
		 9,	 9,	 9,	 9,	 9,	 3, 3,	3,	10, 14, 14, 14, 2,	2,	15, 15, 9,	9,	9,	11, 3,	3,
		 10, 14, 14, 14, 7,	 7, 2,	2,	15, 6,	5,	5,	8,	8,	8,	12, 12, 12, 7,	7,	7,	2,
		 2,	 2,	 15, 15, 15, 6, 6,	6,	6,	5,	8,	8,	12, 12, 12, 7,	7,	2,	15, 15, 6,	6,
		 6,	 5,	 1,	 1,	 1,	 1, 1,	1,	1,	7,	2,	2,	15, 6,	6,	5,	8,	1,	1,	1,	1,	1,
		 1,	 1,	 1,	 1,	 2,	 2, 15, 15, 6,	6,	6,	5,	8,	8,	1,	1,	1,	1,	1,	7,	2,	2,
		 15, 15, 15, 6,	 6,	 6, 6,	8,	8,	12, 12, 12, 7,	7,	7,	2,	15, 6,	9,	6,	5,	8,
		 10, 14, 14, 14, 14, 7, 7,	7,	2,	2,	15, 9,	9,	9,	9,	11, 11, 11, 13, 13, 10, 10,
		 14, 14, 14, 4,	 9,	 9, 9,	9,	9,	9,	9,	11, 11, 11, 11, 11, 11, 9,	9,	9,	11, 11,
		 11, 11, 6,	 5,	 5,	 5, 8,	8,	6,	6,	6,	6,	5,	15,
	 }},
	/* 55974/55976 dashboard pixels agree; first 16 car colors only. */
	{"STDBFGTO",
	 {
		 0,	 1,	 2,	 3,	 4,	 5,	 6,	 7,	 2,	 9,	 10, 11, 12, 13, 14, 15, 15, 13, 13, 13, 13, 14,
		 10, 9,	 8,	 8,	 3,	 2,	 7,	 6,	 5,	 5,	 15, 13, 13, 12, 11, 11, 11, 11, 11, 4,	 4,	 4,
		 4,	 4,	 5,	 5,	 15, 15, 13, 12, 12, 12, 12, 12, 12, 12, 4,	 4,	 4,	 7,	 6,	 5,	 15, 15,
		 15, 13, 12, 12, 12, 12, 12, 12, 12, 12, 2,	 7,	 6,	 5,	 15, 15, 13, 13, 13, 12, 12, 12,
		 12, 12, 3,	 2,	 7,	 7,	 6,	 5,	 15, 13, 14, 14, 10, 9,	 9,	 8,	 3,	 2,	 2,	 7,	 6,	 6,
		 5,	 5,	 15, 15, 15, 13, 13, 13, 13, 14, 10, 9,	 8,	 3,	 2,	 7,	 6,	 6,	 15, 15, 13, 13,
		 13, 14, 1,	 1,	 1,	 1,	 1,	 1,	 1,	 7,	 6,	 5,	 15, 13, 13, 13, 10, 1,	 1,	 1,	 1,	 1,
		 1,	 1,	 1,	 1,	 5,	 5,	 15, 15, 13, 13, 13, 14, 10, 9,	 1,	 1,	 1,	 1,	 1,	 7,	 6,	 5,
		 15, 15, 15, 13, 13, 13, 13, 10, 9,	 8,	 8,	 3,	 2,	 7,	 6,	 6,	 15, 13, 13, 13, 14, 10,
		 9,	 8,	 3,	 3,	 2,	 2,	 7,	 6,	 6,	 5,	 15, 13, 13, 13, 13, 14, 12, 12, 12, 12, 12, 12,
		 11, 4,	 4,	 4,	 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 12, 12, 12, 13, 13, 13, 13, 14,
		 12, 12, 13, 14, 14, 14, 10, 10, 13, 13, 13, 13, 14, 15,
	 }},
};

#define AMIGA_ORIGINAL_COLOR_MAP_COUNT                                                             \
	(sizeof(amiga_original_color_maps) / sizeof(amiga_original_color_maps[0]))

#endif
