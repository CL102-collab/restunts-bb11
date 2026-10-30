# Dead-code audit

Audited on 2026-10-30, branch `tracked`, at commit
`314803863f6bc39b4f71489dce6b4f50fa274b35`.

This report records cleanup candidates for later work. Locations refer to that
commit; use symbol names to find them after subsequent changes. No source cleanup
was performed during this audit. All findings below remain pending.

## Evidence and classification

The audit checked C/C++ references, headers, assembly, build manifests, tools,
tests, and native/generated TempleOS HolyC. Generated name mappings and function
definitions were not counted as callers. Platform callbacks and macro-based
calls were checked separately before classifying a function as unused.

There are two distinct categories:

- **No callers:** 23 externally linked functions have no callers in the checked
  repository sources, including tests and platform code.
- **Unused in production:** additional functions or branches are exercised by
  tests but have no reachable gameplay callers. Their tests do not establish
  production use.

This is a source audit, not an exhaustive proof that every unused symbol in the
repository has been found. Recheck callers against the current revision before
removing a candidate. Do not use a single-platform linker result alone to decide
whether DOS, Amiga, WebAssembly, or TempleOS code is unused.

## A. Functions with no callers

All 23 functions below have definitions, and sometimes declarations or generated
name mappings, but no call sites or function-pointer registrations were found.
Their translated TempleOS names likewise have no callers.

| Group | Function | Location |
| --- | --- | --- |
| Preview | `run_shape_preview` | `src/restunts/c/restunts.c:650` |
| Timing | `timer_read_key_until_deadline` | `src/restunts/c/timing.c:26` |
| Timing | `timer_read_key_with_timeout` | `src/restunts/c/timing.c:37` |
| Input | `kb_set_readchar_callback` | `src/restunts/c/game_input.c:193` |
| Input | `kb_get_readchar_callback` | `src/restunts/c/game_input.c:198` |
| Input | `joystick_get_scaled_y` | `src/restunts/c/game_input.c:218` |
| Input | `input_wait_for_press_and_release` | `src/restunts/c/game_input.c:862` |
| Math | `sign_word` | `src/restunts/c/math.c:13` |
| Math | `absolute_long` | `src/restunts/c/math.c:22` |
| Math | `vector_interpolate_at_saved_z` | `src/restunts/c/math.c:971` |
| Memory | `mmgr_available_paragraphs` | `src/restunts/c/memmgr.c:227` |
| Memory | `mmgr_allocated_paragraphs` | `src/restunts/c/memmgr.c:232` |
| Memory | `mmgr_has_cached_resource` | `src/restunts/c/memmgr.c:461` |
| Memory | `locate_many_shapes_nofatal` | `src/restunts/c/memmgr.c:659` |
| Memory | `locate_many_sounds_fatal` | `src/restunts/c/memmgr.c:664` |
| Memory | `locate_many_resources_optional` | `src/restunts/c/memmgr.c:673` |
| Audio | `audio_effect_range_idle` | `src/restunts/c/audio_engine.c:490` |
| Audio | `audio_play_default_effect` | `src/restunts/c/audio_engine.c:1234` |
| Audio | `audio_timer_effect_idle` | `src/restunts/c/audio_engine.c:1273` |
| Audio | `audio_set_music_volume` | `src/restunts/c/audio_engine.c:1444` |
| Audio | `audio_get_channel_value` | `src/restunts/c/audio_engine.c:1450` |
| Audio | `audio_get_channel_note` | `src/restunts/c/audio_engine.c:1455` |
| Audio resources | `audio_set_missing_file_fatal` | `src/restunts/c/audio_resources.c:807` |

### Related code that can become removable with these functions

- `run_shape_preview` is a 95-line abandoned preview loop. Its dedicated globals
  `shaperect`, `transshape`, `cliprect`, and `carpos` are at `restunts.c:183`;
  declarations are in `menu_internal.h:150`. Check its preview-only constants
  around `restunts.c:102` and their initializers when removing the loop.
- The static `input_readchar_callback` pointer at `game_input.c:78` is read and
  written only by the unused getter/setter. SDL, Amiga, DOS, and TempleOS
  `kb_call_readchar_callback` implementations call `kb_read_char` directly.
- `vector_saved_z_low` and `vector_saved_z_high` are used only by
  `vector_interpolate_at_saved_z`. Locate their storage definitions as well as
  their declarations at `math.c:968`.
- Static `locate_many_resources_nofatal` at `memmgr.c:649` is called only by the
  unused memory wrappers above. It is an additional dependent helper, not one of
  the 23 zero-caller functions.
- Static `audio_missing_file_fatal` at `audio_resources.c:58` starts at zero.
  Its only writer is the unused setter. Therefore the fatal-error branches in
  `load_audio_file` at line 308 and `load_song_file` at line 322 are unreachable.
  Preserve the live audio-loading and fallback behavior when simplifying them.

## B. Remaining fractional prediction support

The predictor implementation and header were already removed before this audit.
Related fractional-only paths remain in the production sources.

At `src/restunts/c/stateply.c:78`, static `player_motion_fraction` is
zero-initialized and has no production assignments. Only tests that include the
implementation directly change it. `player_motion_state` at line 79 likewise
remains permanently `&state` in production.

Consequently, the nonzero-fraction paths for motion scaling, disposable crash
handling, inherited contact-residual relaxation, and fractional suspension are
unreachable during gameplay. Relevant locations include:

- `scale_player_fraction`, `scale_player_motion`, and `player_tick_fraction`
  near `stateply.c:81`.
- The fractional branch in `player_motion_crash` near `stateply.c:109`.
- Inherited/relaxed wheel-plane residual helpers around `stateply.c:992`.
- The fractional suspension branch at `stateply.c:1169`.
- Other `player_motion_fraction` guards throughout the same file.

Native object inspection supported this result: the optimized `stateply.c`
object retained the ordinary suspension reference and no reference to
`update_wheel_suspension_fraction`. GCC had folded away the unused fraction
branch. Source reachability, rather than that optimization alone, is the basis
for the finding.

| Function | Location | Remaining use |
| --- | --- | --- |
| `update_car_speed_fraction` | `src/restunts/c/statecar.c:387` | `test-car-speed.c` only |
| `update_player_steering_fraction` | `src/restunts/c/opponent.c:624` | `test-route-points.c` only |
| `update_follow_cameras_fraction` | `src/restunts/c/gamestep.c:168` | `test-simulation-setup.c` only |
| `update_wheel_suspension_fraction` | `src/restunts/c/physics_collision.c:714` | Tests and the unreachable `stateply.c` branch |

Production steering always supplies a full tick to its shared steering helper;
production follow cameras use the ordinary update path with a zero fraction.
The full-tick physics helpers remain live and must be preserved.

Some tests mix useful authoritative-physics assertions with fractional-only
coverage. In particular, `test-fractional-motion.c` includes original cornering
bias checks. Preserve or relocate relevant gameplay assertions when removing
obsolete fractional tests. Shared helpers, constants, and header declarations
should be reconsidered after all their fractional-only users are removed.

## C. Other test-only production APIs and storage

### Extended-track origin lookup: 32,400 bytes

`EXTENDED_TRACK_STORAGE.origins` at `src/restunts/c/extended_track.c:37` contains
8,100 `int32_t` entries. Storage preparation initializes it at line 256 and writes
it at lines 283-285 on every extended-track load.

Its only lookup consumer outside storage preparation is
`extended_track_object_origin_at` at line 377. That API has two test callers
in `test-extended-track.c:1283` and `:1311`, plus a declaration
and test stub. Production object-by-ID lookups use the separate `origin_heads`
and `origin_next` chain through `extended_track_object_for_id`.

Removing the unused runtime API would allow removing this 32,400-byte table and
its initialization/update work. Preserve the live occupancy and object-by-ID
lookups, including the ordering of objects sharing an anchor.

### CSRPL rewind interpolation

`extended_replay_rewind_interpolate` at
`src/restunts/c/extended_replay.c:214` has four assertions in
`test-csrpl.c:67`, but no production callers. Current CSRPL toolbar scrubbing
previews a target and calls `extended_replay_seek` directly.

The similarly named classic `replay_rewind_interpolate` remains live; it is a
separate function and is not covered by this finding.

### Adaptive cache invalidation

`frame_adaptive_invalidate` at `src/restunts/c/frame_adaptive.c:207` has one
caller, `test-frame-adaptive.c:177`. Production reset/restart paths and cache-key
changes already manage cache validity. This is a small test-only API candidate.

## D. Cases requiring different treatment

- **Unwired shutdown hook:** `hypervision_shutdown` at `hypervision.c:112` is
  called only by `test-hypervision.c:586`, but frees live renderer command,
  edge, and bin allocations. Production `sdl3_platform_shutdown` at
  `platform/sdl3/input.c:1262` calls `hires_shutdown`, which does not call it.
  Investigate ownership and whether to connect this cleanup to shutdown rather
  than deleting it solely because it currently lacks a production caller.
- **Live intro module:** `intro_render.c` is used through
  `intro.c:160 -> setup_intro -> intro_render_session -> intro_render_scene_impl`.
  The public `intro_render_scene` at `intro_render.c:178` is a test-facing adapter.
  Do not infer that the whole module or its shared helpers are dead from that
  adapter's caller count.
- **Fixture/construction APIs:** `extended_track_activate` and the CSTRK
  construction helpers have test users. They were not classified as accidental
  dead code merely because gameplay loads serialized tracks instead.
- **Macro/assembly-facing routines:** several legacy arithmetic and resource
  functions have no direct C call spelling but are used through macros or
  assembly. Those were excluded from the confirmed zero-caller inventory.

## Suggested cleanup batches and verification

1. Remove the zero-caller functions in section A with their exclusively owned
   globals, constants, declarations, and dependent static helpers.
2. Remove fractional-only prediction machinery as one coherent change, retaining
   the original full-tick physics behavior and relevant regression assertions.
3. Remove the unused origin API/table and the small test-only APIs in section C.
4. Investigate the HyperVision cleanup lifecycle separately.

For each batch, recheck source and translated-name callers, including function
pointers and platform variants. For the static fraction assignment check, use
`rg -nP 'player_motion_fraction\s*=(?!=)'` so comparisons are not mistaken for
assignments. Search tests separately from production callers.

Follow `AGENTS.md`, `.gitattributes`, and the formatting guidance in `readme.md`.
Regenerate affected TempleOS artifacts using its documented translator workflow;
the audit also observed pre-existing generation drift outside these candidates,
so review generated changes for scope. Preserve third-party/reference sources.

Build the native targets and run the affected host, replay, renderer, and audio
regressions. Physics/rendering changes should retain deterministic replay state
and pixel output. Check classic and SDL3 configurations and the relevant
DOS/Amiga/TempleOS interfaces. Update this report with removal commits or new
callers so future cleanup does not repeat the audit.
