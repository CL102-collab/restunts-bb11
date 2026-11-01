include ../watcom.mk
TARGET ?= dosfull
OBJDIR = build/watcom/$(CONFIG)/$(TARGET)
ifeq ($(TARGET),dosheadless)
  CFLAGS = -dRESTUNTS_HEADLESS
else ifeq ($(TARGET),dosfull)
  CFLAGS = -dRESTUNTS_FULL
else
  $(error Unsupported C target: $(TARGET))
endif

C_FILES = fileio.c file_car.c file_decomp.c legacy.c resource.c math/math.c math/math_rectangles.c memmgr.c strlib.c numlib.c kevinrnd.c replinit.c gameinit.c gamestep.c simd.c car/carsetup.c trkutil.c track/trackres.c restunts.c startup.c fatal.c timing.c game_input.c game_input_shortcuts.c ui/ui_text.c ui/ui_input.c ui/ui_dialog.c ui/ui_file_dialog.c ui/ui_disk_dialog.c intro.c highscore/highscore.c highscore/highscore_results.c highscore/highscore_results_text.c replay/replay_record.c replay/replay_viewer.c replay/replay_toolbar.c replay/replay_menu.c replay/replay_scrub.c race/race.c race/race_rewind.c race/race_presentation.c ghost/ghost.c ghost/ghost_line.c race/race_resources.c dashboard.c menu/menu_common.c menu/menu_main.c menu/menu_options.c menu/menu_track.c menu/menu_track_editor.c menu/menu_track_editor_map.c menu/menu_track_editor_render.c menu/menu_track_editor_input.c menu/menu_opponent.c menu/menu_car.c audio/audio_engine.c audio/audio_channels.c audio/audio_context.c audio/audio_resources.c audio/audio_car.c audio/audio_sequence.c shape3d/shape3d.c shape3d/shape3d_queue.c shape3d/shape3d_primitives.c shape3d/shape3d_render.c shape3d/shape3d_resources.c shape3d/shape3d_car.c shape3d/shape3d_prerender.c shape3d/shape3d_polygon.c shape3d/shape3d_lines.c shape3d/shape3d_line_setup.c shape2d/shape2d.c shape2d/shape2d_image.c shape2d/shape2d_blit.c shape2d/shape2d_window.c shape2d/shape2d_resources.c heapsort.c \
		  frame/frame.c frame/frame_camera.c frame/frame_car.c frame/frame_tiles.c frame/frame_terrain.c frame/frame_objects.c skybox.c frame/frame_overlay.c intro_render.c track/track_preview.c state.c race/race_route.c physics/physics_grip.c opponent/opponent.c track/track_setup.c stateply.c stateply_motion.c stateply_contact.c physics/physics_collision.c physics/physics_suspension.c statecrs.c statecar.c track/trackobj.c track/trackobj_road.c track/trackobj_stunts.c track/trackobj_queries.c owoot/owoot.c owoot/owoot_wheels.c owoot/owoot_road.c owoot/owoot_route.c replay/replay.c stateio.c hires/settings_startup.c hires/settings_input.c hires/race_view.c hires/race_presentation.c hires/ghost_render.c hires/frame_visibility.c hires/frame_snapshot.c hires/frame_integration.c hires/frame_car_render.c hires/frame_supersight.c hires/frame_terrain.c hires/frame_particles.c hires/frame_overlay.c hires/intro_presentation.c hires/track_preview_scene.c hires/showroom.c hires/shape3d_pipeline.c hires/shape3d_queue.c hires/shape3d_render.c hires/highscore_animation.c hires/skybox_scene.c frame/frame_context.c frame/frame_fps.c extended/extended_math.c extended/extended_frame_layout.c

DOS_STARTUP_OBJ =

ifeq ($(TARGET),dosheadless)
  C_FILES = fileio.c file_car.c file_decomp.c legacy.c resource.c math/math.c math/math_rectangles.c memmgr.c strlib.c numlib.c kevinrnd.c replinit.c gameinit.c gamestep.c simd.c car/carsetup.c trkutil.c track/trackres.c headless.c \
			headless_data.c headless_trackdata.c state.c race/race_route.c physics/physics_grip.c opponent/opponent.c track/track_setup.c stateply.c stateply_motion.c stateply_contact.c physics/physics_collision.c physics/physics_suspension.c statecrs.c statecar.c track/trackobj.c track/trackobj_road.c track/trackobj_stunts.c track/trackobj_queries.c owoot/owoot.c owoot/owoot_wheels.c owoot/owoot_road.c owoot/owoot_route.c replay/replay.c stateio.c extended/extended_math.c
  DOS_STARTUP_OBJ = $(OBJDIR)/dosstart.obj
endif

ifeq ($(TARGET),dosfull)
  C_FILES += command_line.c video_pages.c headless_data.c headless_trackdata.c data/full_data.c data/full_strings.c data/full_tables.c
  DOS_STARTUP_OBJ = $(OBJDIR)/dosstart.obj
endif

# Keep extracted format-aware hooks with their classic entry points. Most
# become no-ops on DOS; the shared OWOOT traversal engine also serves legacy tracks.
C_FILES += extended/extended_gameinit.c extended/extended_gamestep.c extended/extended_trkutil.c extended/extended_race_route.c extended/extended_track_setup.c extended/extended_trackobj.c extended/extended_trackobj_solids.c extended/extended_stateply.c extended/extended_physics_collision.c extended/extended_statecrs.c extended/extended_owoot_route.c extended/extended_owoot_gates.c extended/extended_owoot_jump.c extended/extended_owoot_road.c extended/extended_opponent.c extended/extended_fileio.c
ifeq ($(TARGET),dosfull)
  C_FILES += extended/extended_menu_runtime.c extended/extended_highscore_ui.c extended/extended_ui_dialog.c extended/extended_replay_ui.c extended/extended_ghost_runtime.c hires/track_preview_mode.c
endif

OBJ_FILES = $(addprefix $(OBJDIR)/, $(C_FILES:.c=.obj)) $(DOS_STARTUP_OBJ)
OBJ_SUBDIRS = $(filter-out $(OBJDIR),$(sort $(patsubst %/,%,$(dir $(OBJ_FILES)))))
C_HEADERS = $(wildcard *.h */*.h)
GIT_REVISION_HEADER = $(OBJDIR)/git_revision.h
ifeq ($(OS),Windows_NT)
  WRITE_GIT_REVISION_HEADER = echo \#define RESTUNTS_GIT_HASH "$(GIT_REVISION)"> "$(subst /,\,$@)"
else
  WRITE_GIT_REVISION_HEADER = printf '\#define RESTUNTS_GIT_HASH "%s"\r\n' "$(GIT_REVISION)" > "$@"
endif

.PHONY: all clean force-git-revision
all: $(OBJ_FILES)

$(OBJDIR):
	$(call MKDIR,$@)

# Create each shared output directory once, before any parallel compiler uses it.
# The Windows MKDIR command is not safe when two recipes create the same path.
ifneq ($(strip $(OBJ_SUBDIRS)),)
$(OBJ_SUBDIRS): | $(OBJDIR)
	$(call MKDIR,$@)
endif

# Check HEAD on every build, updating the header only when the commit changes.
force-git-revision:

$(GIT_REVISION_HEADER): force-git-revision | $(OBJDIR)
	$(eval GIT_REVISION := $(shell git rev-parse --verify HEAD))
	$(if $(GIT_REVISION),,$(error Cannot determine the build commit; Git and a valid repository HEAD are required))
	$(if $(findstring "$(GIT_REVISION)",$(file <$@)),,$(WRITE_GIT_REVISION_HEADER))

# Refresh the compilation date even when the commit and sources are unchanged.
$(OBJDIR)/menu/menu_options.obj $(OBJDIR)/command_line.obj: $(GIT_REVISION_HEADER) force-git-revision
$(OBJDIR)/menu/menu_options.obj $(OBJDIR)/command_line.obj: CFLAGS += -fi=$(GIT_REVISION_HEADER)

# Ghost guidance uses floating-point geometry; the custom DOS startup does not
# initialize an 8087 or the CRT's floating-point formatting support.
$(OBJDIR)/ghost/ghost.obj $(OBJDIR)/ghost/ghost_line.obj: CFLAGS += -fpc -zls

.SECONDEXPANSION:
$(OBJDIR)/%.obj: %.c $(C_HEADERS) makefile ../watcom.mk $(WATCOM_STAMP) | $$(@D)
	$(CC) $(WATCOM_CFLAGS) $(CFLAGS) -fo=$@ $<

$(OBJDIR)/dosstart.obj: ../platform/dos/dosstart.c $(C_HEADERS) $(wildcard ../platform/dos/*.h) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_PLATFORM_CFLAGS) $(CFLAGS) -i=. -fo=$@ $<

clean:
	$(call RMDIR,$(OBJDIR))
