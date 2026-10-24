include ../watcom.mk

TARGET ?= headless
ifeq (,$(filter $(TARGET),headless original))
  $(error Unsupported repldump target: $(TARGET))
endif

RESTUNTS_OBJ_FILES = fileio.obj legacy.obj resource.obj math.obj memmgr.obj strlib.obj numlib.obj \
                     kevinrnd.obj replinit.obj gameinit.obj gamestep.obj \
                     simd.obj carsetup.obj trkutil.obj trackres.obj headless.obj dosstart.obj headless_data.obj headless_trackdata.obj state.obj race_route.obj physics_grip.obj opponent.obj track_setup.obj stateply.obj physics_collision.obj \
                     statecrs.obj statecar.obj trackobj.obj owoot.obj owoot_wheels.obj owoot_road.obj owoot_route.obj replay.obj stateio.obj

PLATFORM_DIR = ../platform/dos/build/watcom/$(CONFIG)
PLATFORM_ASM_DIR = $(PLATFORM_DIR)/wasm
PLATFORM_OBJECTS = $(addprefix $(PLATFORM_DIR)/,$(addsuffix .obj,$(PLATFORM_NAMES)))
ifeq ($(TARGET),original)
include ../asmorig/link.mk
OBJDIR = build/watcom/$(CONFIG)/original
OUTFILE = repldumo.exe
DESTFILE = repldumo.exe
PLATFORM_NAMES = runtime
C_LINK_OBJECTS = $(OBJDIR)/repldump.obj $(PLATFORM_OBJECTS)
OBJ_FILES = $(ASMORIG_LINK_OBJECTS) $(C_LINK_OBJECTS)
START_OPTION = $(ORIGINAL_LINK_OPTIONS)
LINK_ORDER = $(ORIGINAL_LINK_ORDER)
CFLAGS = -dRESTUNTS_ORIGINAL
# The wrapper's inline DOS I/O needs the platform's unoptimized code generation.
WRAPPER_CFLAGS = $(WATCOM_PLATFORM_CFLAGS)
ALIAS_SCRIPT = ../../../tools/scripts/generate-original-link-aliases.py
ALIAS_FILE = $(OBJDIR)/aliases.lnk
LINK_ALIASES = @$(ALIAS_FILE)
else
OBJDIR = build/watcom/$(CONFIG)
OUTFILE = repldump.exe
DESTFILE = repldump.exe
C_DIR = ../c/build/watcom/$(CONFIG)/dosheadless
PLATFORM_NAMES = runtime memory file divide
OBJ_FILES = $(PLATFORM_ASM_DIR)/dosbss.obj $(addprefix $(C_DIR)/,$(RESTUNTS_OBJ_FILES)) $(OBJDIR)/repldump.obj $(PLATFORM_OBJECTS) $(PLATFORM_ASM_DIR)/dosstack.obj
START_OPTION = option START=_headless_start
LINK_ORDER = $(WATCOM_PORTED_LINK_ORDER)
CFLAGS = -dRESTUNTS_HEADLESS
WRAPPER_CFLAGS = $(WATCOM_CFLAGS)
endif
SPACE := $(empty) $(empty)
COMMA := ,

.PHONY: all clean
all: $(OBJDIR) $(OBJ_FILES) $(ALIAS_FILE) $(MAKEFILE_LIST)
	$(file >$(OBJDIR)/link.lnk,name $(OUTFILE))
	$(file >>$(OBJDIR)/link.lnk,format dos $(WATCOM_LFLAGS) $(START_OPTION) option MAP=$(basename $(OUTFILE)).map)
	$(file >>$(OBJDIR)/link.lnk,$(LINK_ORDER))
	$(file >>$(OBJDIR)/link.lnk,$(LINK_ALIASES))
	$(file >>$(OBJDIR)/link.lnk,$(WATCOM_LIBPATH))
	$(file >>$(OBJDIR)/link.lnk,file $(subst $(SPACE),$(COMMA),$(OBJ_FILES)))
	$(WLINK) @$(OBJDIR)/link.lnk
	$(call COPY,$(OUTFILE),../../../stunts/$(DESTFILE))

$(OBJDIR):
	$(call MKDIR,$@)

ifeq ($(TARGET),original)
$(ALIAS_FILE): $(ASMORIG_LINK_OBJECTS) $(C_LINK_OBJECTS) $(ALIAS_SCRIPT) $(MAKEFILE_LIST) $(WATCOM_STAMP) | $(OBJDIR)
	$(PYTHON) $(ALIAS_SCRIPT) --asm-dir "$(ASMORIG_DIR)" --output "$@" $(C_LINK_OBJECTS)
endif

$(OBJDIR)/%.obj: %.c $(wildcard ../c/*.h) $(wildcard ../platform/dos/*.h) $(wildcard *.h) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WRAPPER_CFLAGS) $(CFLAGS) -i=../c -fo=$@ $<

clean:
	$(call RMDIR,build/watcom)
	$(call REMOVE,*.exe)
	$(call REMOVE,*.map)
