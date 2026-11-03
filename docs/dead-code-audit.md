# Dead-code audit

Revalidated on 2026-11-03 against `cont-driv-3`, starting at
`d129e8d67096f863108437853a6ad266b8b49d29`. This report documents the completed
cleanup and its validation. That cleanup was committed as `3df26b0c`; the further
cleanup against that revision is documented in the follow-up section.

The original audit covered `314803863f6bc39b4f71489dce6b4f50fa274b35` on
2026-10-30. Subsequent source splits moved several functions and renamed the
renderer APIs, so its old file locations and caller counts were rechecked.
The confirmed findings in sections A-C have now been removed, together with
additional unused dependencies and renderer helpers.

## Evidence and boundaries

Caller checks covered production C/C++, headers, tests, build manifests, macros,
assembly interfaces, platform implementations, and native/translated TempleOS
names. Definitions, declarations, and generated name mappings were not counted as
callers. A single-platform linker result was not used as proof of dead code.

Third-party code and original assembly reference sources are preserved. APIs used
for constructing test fixtures are retained where they support meaningful checks
of live behavior. This remains a source audit, not a proof that the entire
repository contains no other dead code.

## A. Removed functions with no callers

All 23 original zero-caller findings remained unused:

| Area | Removed functions |
| --- | --- |
| Preview | `run_shape_preview` |
| Timing | `timer_read_key_until_deadline`, `timer_read_key_with_timeout` |
| Input | `kb_set_readchar_callback`, `kb_get_readchar_callback`, `joystick_get_scaled_y`, `input_wait_for_press_and_release` |
| Math | `sign_word`, `absolute_long`, `vector_interpolate_at_saved_z` |
| Memory | `mmgr_available_paragraphs`, `mmgr_allocated_paragraphs`, `mmgr_has_cached_resource`, `locate_many_shapes_nofatal`, `locate_many_sounds_fatal`, `locate_many_resources_optional` |
| Audio | `audio_effect_range_idle`, `audio_play_default_effect`, `audio_timer_effect_idle`, `audio_set_music_volume`, `audio_get_channel_value`, `audio_get_channel_note` |
| Audio resources | `audio_set_missing_file_fatal` |

Their exclusive dependencies were removed too:

- The abandoned preview loop's globals, constants, and initializers.
- The unused read-character callback type and pointer. Platform callback entry
  points still call `kb_read_char` directly.
- Saved-Z interpolation storage and obsolete fixture definitions.
- Static `locate_many_resources_nofatal`, and `locate_sound_fatal` after its only
  caller was removed. The latter's private fatal branch and error string are gone;
  live optional sound lookup remains.
- The never-enabled missing-audio-file fatal flag and its unreachable branches.
  Audio file fallback and fatal driver-loading behavior remain.
- Getter-only `audio_channel_values` and `audio_channel_notes` arrays and their
  writes: 48 bytes of unused channel bookkeeping. The engine regression fingerprint
  now excludes that deleted bookkeeping; its remaining trace was captured before
  cleanup and matches afterward.

## B. Removed obsolete fractional prediction support

Production `player_motion_fraction` was permanently zero and
`player_motion_state` permanently pointed to `state`. Only implementation-including
tests changed them. The unused speculative paths and their storage were removed:

- Motion/time scaling and the alternative disposable crash-state path.
- Inherited contact-residual relaxation and fractional suspension branches.
- `update_car_speed_fraction`, `update_player_steering_fraction`,
  `update_follow_cameras_fraction`, and `update_wheel_suspension_fraction`.
- Unneeded fractional parameters and Q16 tick constants in shared helpers.
- `extended_player_contact_distance`, whose only production call was in the
  removed inherited-residual path.

The ordinary 10 Hz and 20 Hz updates remain. Extended roll-ramp momentum retains
its live subangle remainder and rate filtering, including two filter steps per
10 Hz tick. Geometric interpolation and contact sweep fractions are still live
and were not treated as prediction machinery.

Authoritative speed, steering, camera, and suspension assertions remain; obsolete
fraction-only assertions were removed. Original cornering-bias and inverted-wheel
coverage from `test-fractional-motion.c` is retained in `test-player-motion.c`.
The host runner uses the new name.

## C. Removed test-only runtime APIs and storage

### Extended-track origin lookup

Removed `extended_track_object_origin_at` and the `origins` array, its
initialization, and its per-load updates. This saves 8,100 32-bit entries
(**32,400 bytes**) per active extended-track storage allocation.

Production occupancy and the `origin_heads`/`origin_next` object-ID chains remain.
Tests now use the live model and `extended_track_object_for_id` APIs. Occupancy,
sparse footprints, and ordering for objects sharing an anchor remain covered.

### Replay and adaptive rendering

Removed `extended_replay_rewind_interpolate` and its obsolete assertions. Current
CSRPL scrubbing still seeks directly; classic `replay_rewind_interpolate` remains
live and unchanged.

The original `frame_adaptive_invalidate` finding had become
`frame_hires_adaptive_invalidate`. It was still called only by its test and is now
removed. Production resets and cache-key invalidation remain covered.

## Additional confirmed unused helpers

Current-source checks found eight more zero-caller functions:

- `extended_geometry_grade_vector`; the live grade-delta helper remains.
- `shape3d_hires_vertex_rect_flags`.
- `shape3d_hires_wheel_face`, together with the write-only
  `HIRES_PRIMITIVE.wheel_face` field and assignment.
- `divide_u32_to_u16`, `projection_multiply_x`, `projection_multiply_y`,
  `projection_scale_y`, and `projection_set_half_fov`.

## Retained cases

- The old `hypervision_shutdown` name is now `hires_backend_shutdown`. It still
  owns live renderer command, edge, and bin allocations and is retained. Its
  missing connection to production shutdown is a separate lifecycle issue.
- The intro rendering module remains reachable through intro setup/session code.
  Its test adapter does not make the implementation dead.
- Extended-track activation and CSTRK construction APIs still support fixtures.
- Macro- and assembly-facing arithmetic/resource routines remain when callers
  exist through those interfaces.

## Generated TempleOS output: first cleanup

Shared-source changes require regeneration with `tools/TRANSLATE.PY` and the
pinned dependencies documented in the port README; generated files are not edited
by hand. Temporary identifiers use a global serial, so deletions renumber many
otherwise unchanged generated functions.

A separate translation of the starting revision identified pre-existing drift in
159 generated files. After normalizing temporary numbering, only
`REPLAY_MENU.HC` differed substantively: it had not yet incorporated the earlier
camera-continuation cherry-pick. Complete regeneration includes that existing
source change alongside this cleanup. Baseline-versus-current translation is
compared separately to distinguish cleanup from numbering and pre-existing drift.

## Validation: first cleanup

- Configured native Linux targets build successfully; the launcher is disabled
  for the headless environment.
- All 74 CTest checks pass across the initial run and isolated reruns. The initial
  parallel run passed 72; `genghost-search` and `genghost-parallel` reached their
  subprocess time limits. Both pass serially with the repository's timeout
  multiplier set to 3; no assertion or source change was needed.
- The host suite includes classic, SDL3, Amiga, and relevant DOS interface checks.
  Targeted input, startup, memory, resource, audio, and physics tests also pass.
- Original full-tick speed, steering, and suspension fingerprints remain
  unchanged. The live audio trace matches its pre-cleanup capture.
- Before/after replay comparison against preserved starting-revision libraries
  matches all 241 serialized game-state/RNG records in each of four renderer
  modes and all 4,031 full-frame pixel digests. This bounded comparison uses
  `DEFAULT.RPL`; optional crash, hard-landing, and shaking replay fixtures were
  unavailable as loose files. Extended slope and zero-speed recordings pass the
  replay-engine regression.
- TempleOS regeneration is reproducible with `TRANSLATE.PY --check`; its 25
  translator cleanup tests and HolyC style check pass. After temporary-number
  normalization, 23 generated files contain substantive cleanup changes.
- Pinned C/H formatting, EditorConfig, CRLF/file-mode checks, and
  `git diff --check` pass.

Guest execution on DOS, Amiga, or TempleOS was not performed. Native-generated
interfaces and translated source were checked as described above. Replay capture
and generated-scope evidence is retained locally under ignored
`out/dead-code-baseline/` and `out/dead-code-generated-scope.txt`.

## Follow-up audit against `3df26b0c`

A second pass on 2026-11-03 checked the remaining shared and platform functions,
private state, runtime fields, declarations, and tool references. It starts at
`3df26b0c16551c2e8a8e3246516cddd999392454`. The completed changes and their
validation are documented below.

### Additional unused APIs removed

The follow-up removes 25 unused API names. Counts below refer to names, since the
video wrapper had separate implementations on several platforms.

| Area | Removed APIs |
| --- | --- |
| Sprites and rasterization | `sprite_set_clip_bounds`, `draw_two_color_pattern_lines`, `shape2d_rle_mask_at_anchor`, `shape2d_rle_mask`, `shape2d_rle_copy_at_anchor`, `shape2d_rle_copy_anchor_clipped`, `sprite_putimage_at_shape_anchor`, `sprite_capture_at_anchor`, `shape2d_copy_at_anchor`, `shape2d_mask_at_anchor`, `file_get_shape2d_identifier`, `wheel_fill_polygon` |
| File loading | `file_decomp_fatal`, `file_decomp_paras_fatal` |
| Timing | `timer_custom_delta`, `timer_reset` |
| Audio | `audioresource_copy_4_bytes`, `audio_play_effect`, `audio_play_engine_start`, `audio_play_engine_stop`, `audio_play_engine_failure` |
| Platform video | `video_get_status`: SDL3, DOS, Amiga, and handwritten TempleOS wrapper |
| Ghost generation | `genghost_maneuver_name`, its only dependency `extended_genghost_maneuver_name`, and `genghost_route_piece_height` |

Two exclusively owned static sprite adapters were also removed:
`shape2d_render_rle_at_anchor` and `sprite_shape_to_1_at_anchor`. Shared anchor
math and the live masked/OR drawing paths remain.

The generic decompression helpers and their fatal branches still have callers.
Actual video polling uses `dos_video_get_status`, which remains on all platforms.
The live extended route-height implementation also remains. Assembly references
to the removed wrappers belong to the isolated original-engine target, which
links its own original definitions.

The audio sequence regression now calls the live rate-aware effect helper with
the same current rate. Only the three obsolete engine convenience cases were
removed from the feedback test; live effects, engine initialization, and native
audio assertions remain. The classic audio fingerprint is unchanged.

### Write-only state and stale declarations removed

- Private startup `unused_nd_option` storage. `/nd` remains an accepted no-op;
  startup regression traces and other option assertions remain unchanged.
- Unread `pspofs`, `pspseg`, and `reserved_race_word` storage and assignments.
  DOS process-information and memory APIs remain; only discarded copies are gone.
- Unused `rect_ingame_text3` and `rect_ingame_text4` rectangles.
- Write-only `direction_sector_sine_copy` and `direction_sector_cosine_copy`,
  including their duplicate pure trigonometric calls. Live thresholds remain.
- Permanently zero `replay_legacy_play_active` and
  `replay_legacy_fast_play_active`, and their unreachable pause-selection branch.
  Replay control drawing and normal input handling remain.
- `EXTENDED_REPLAY_CLOCKS.opponent_end` and `GENGHOST_MANEUVER.feature_begin`.
  These were assigned but never consumed. The clock and maneuver structures are
  internal, with no corresponding raw on-disk format or packed driver ABI.
- The declaration-only `unused_legacy_word` and 16 stale string/error externs
  without definitions or callers.

Two live implementation helpers, `extended_replay_reserve` and
`extended_track_rebind`, are now private. Their implementations remain; unused
public declarations and a test stub were removed.

The tool cleanup removes an unused `shutil` import from
`analyze-genghost-stunts.py`, `SPEC_SOURCE` alias from the McMurtry generator,
`REGISTERS` set from `prepare-wasm-original.py`, and `RULE_LEGACY_COLLISION`
constant from `test-genghost-extended.py`.

### Interfaces deliberately retained

Callback registration/removal and the default-SI player-tick adapter still support
meaningful lifecycle and compatibility checks. Audio finish-callback and byte-copy
helpers support native pointer-marshaling tests; packed audio fields and layouts
are unchanged. `mcmurtry_dashboard.SHAPE_HEADER_SIZE` is a live re-export consumed
by the generator and remains imported. Renderer shutdown ownership and fixture
construction APIs remain as documented above.

### Follow-up validation

- Configured native Linux targets build successfully.
- Focused startup, decompression, memory, race, sprite, audio, and extended replay
  tests pass, as do all 17 WASM preparation tests and McMurtry asset verification.
  Startup and classic audio fingerprints remain unchanged.
- Before/after comparison against the immediate `3df26b0c` baseline preserves all
  241 game-state/RNG records in each of four modes and all 4,031 full-frame pixel
  digests. The compared library matches the current native build byte-for-byte.
- Full TempleOS regeneration passes `--check`, all 25 translator cleanup tests,
  and the HolyC style check. It updates 153 generated files; after normalizing
  temporary names, only 22 contain substantive changes. Shared DOS/TempleOS
  function definitions decrease from 2,268 to 2,245.
- Pinned C/H formatting, EditorConfig, file endings/modes, and whitespace checks
  pass. Packed audio layouts, original-engine assembly, and third-party sources
  remain unchanged.
- All 74 CTest checks pass in one run with timeout multiplier 3, including
  the host suite, ghost searches, platform, audio, replay, and rendering checks.

Local before/after captures and normalized generated-scope evidence are retained
under ignored `out/dead-code-round2/`.
