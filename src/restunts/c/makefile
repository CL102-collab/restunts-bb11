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

C_FILES = fileio.c legacy.c resource.c math.c memmgr.c strlib.c numlib.c kevinrnd.c replinit.c gameinit.c gamestep.c simd.c car/carsetup.c trkutil.c track/trackres.c restunts.c fatal.c timing.c game_input.c ui/ui_text.c ui/ui_input.c ui/ui_dialog.c intro.c highscore.c replay/replay_record.c replay/replay_viewer.c race/race.c ghost.c race/race_resources.c dashboard.c menu/menu_common.c menu/menu_main.c menu/menu_options.c menu/menu_track.c menu/menu_track_editor.c menu/menu_opponent.c menu/menu_car.c audio/audio_engine.c audio/audio_resources.c audio/audio_car.c audio/audio_sequence.c shape3d/shape3d.c shape3d/shape3d_resources.c shape3d/shape3d_car.c shape3d/shape3d_prerender.c shape3d/shape3d_lines.c shape2d/shape2d.c shape2d/shape2d_blit.c shape2d/shape2d_resources.c heapsort.c \
		  frame.c skybox.c frame_overlay.c intro_render.c track/track_preview.c state.c race/race_route.c physics_grip.c opponent/opponent.c track/track_setup.c stateply.c physics_collision.c statecrs.c statecar.c track/trackobj.c owoot/owoot.c owoot/owoot_wheels.c owoot/owoot_road.c owoot/owoot_route.c replay/replay.c stateio.c

DOS_STARTUP_OBJ =

ifeq ($(TARGET),dosheadless)
  C_FILES = fileio.c legacy.c resource.c math.c memmgr.c strlib.c numlib.c kevinrnd.c replinit.c gameinit.c gamestep.c simd.c car/carsetup.c trkutil.c track/trackres.c headless.c \
			headless_data.c headless_trackdata.c state.c race/race_route.c physics_grip.c opponent/opponent.c track/track_setup.c stateply.c physics_collision.c statecrs.c statecar.c track/trackobj.c owoot/owoot.c owoot/owoot_wheels.c owoot/owoot_road.c owoot/owoot_route.c replay/replay.c stateio.c
  DOS_STARTUP_OBJ = $(OBJDIR)/dosstart.obj
endif

ifeq ($(TARGET),dosfull)
  C_FILES += command_line.c video_pages.c headless_data.c headless_trackdata.c full_data.c full_strings.c full_tables.c
  DOS_STARTUP_OBJ = $(OBJDIR)/dosstart.obj
endif

# Keep extracted format-aware hooks with their classic entry points. Most
# become no-ops on DOS; the shared OWOOT traversal engine also serves legacy tracks.
C_FILES += extended/extended_gameinit.c extended/extended_gamestep.c extended/extended_trkutil.c extended/extended_race_route.c extended/extended_track_setup.c extended/extended_trackobj.c extended/extended_stateply.c extended/extended_physics_collision.c extended/extended_statecrs.c extended/extended_owoot_route.c extended/extended_owoot_road.c extended/extended_opponent.c extended/extended_fileio.c
ifeq ($(TARGET),dosfull)
  C_FILES += extended/extended_menu_runtime.c extended/extended_highscore_ui.c extended/extended_ui_dialog.c extended/extended_replay_ui.c extended/extended_ghost_runtime.c
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
$(OBJDIR)/ghost.obj: CFLAGS += -fpc -zls

.SECONDEXPANSION:
$(OBJDIR)/%.obj: %.c $(C_HEADERS) makefile ../watcom.mk $(WATCOM_STAMP) | $$(@D)
	$(CC) $(WATCOM_CFLAGS) $(CFLAGS) -fo=$@ $<

$(OBJDIR)/dosstart.obj: ../platform/dos/dosstart.c $(C_HEADERS) $(wildcard ../platform/dos/*.h) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_PLATFORM_CFLAGS) $(CFLAGS) -i=. -fo=$@ $<

clean:
	$(call RMDIR,$(OBJDIR))
