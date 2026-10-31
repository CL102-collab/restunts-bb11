include ../watcom.mk

TARGET ?= ported
ifeq (,$(filter $(TARGET),ported original))
  $(error Unsupported pixldump target: $(TARGET))
endif

# Use the same ported core as the game, replacing only its DOS startup.
include ../dos/full-game-objects.mk
RESTUNTS_OBJ_FILES := $(filter-out dosstart.obj,$(RESTUNTS_OBJ_FILES))

PLATFORM_DIR = ../platform/dos/build/watcom/$(CONFIG)
PLATFORM_ASM_DIR = $(PLATFORM_DIR)/wasm
PLATFORM_OBJECTS = $(addprefix $(PLATFORM_DIR)/,$(addsuffix .obj,$(PLATFORM_NAMES)))
ifeq ($(TARGET),original)
include ../asmorig/link.mk
OBJDIR = build/watcom/$(CONFIG)/original
OUTFILE = pixldumo.exe
DESTFILE = pixldumo.exe
PLATFORM_NAMES = runtime file
C_LINK_OBJECTS = $(OBJDIR)/pixldump.obj $(OBJDIR)/murmur3.obj $(PLATFORM_OBJECTS)
OBJ_FILES = $(ASMORIG_LINK_OBJECTS) $(C_LINK_OBJECTS)
START_OPTION = $(ORIGINAL_LINK_OPTIONS)
# Resource segment words can survive as stopped-opponent wheel headings. Match
# the incremental Borland oracle's 0x39e6 retained paragraphs: original startup
# uses SEG ENDSEG + 1. WLINK rejects code that grows past this heap boundary.
LINK_ORDER = $(ORIGINAL_LINK_ORDER) segaddr=0x39e5 offset=0x0
CFLAGS = -dRESTUNTS_ORIGINAL
# Original rendering observes caller-stack residue. Full local-variable debug
# information (-d2) reshapes those frames even with -os; retain line info instead.
# Far helper calls plus -of retain the archived caller frames as well.
WRAPPER_CFLAGS = $(filter-out -od -os -d2,$(WATCOM_CFLAGS)) -os -of
ifeq ($(CONFIG),debug)
WRAPPER_CFLAGS += -d1
endif
ALIAS_SCRIPT = ../../../tools/scripts/generate-original-link-aliases.py
ALIAS_FILE = $(OBJDIR)/aliases.lnk
LINK_ALIASES = @$(ALIAS_FILE)
else
OBJDIR = build/watcom/$(CONFIG)
OUTFILE = pixldump.exe
DESTFILE = pixldump.exe
C_DIR = ../c/build/watcom/$(CONFIG)/dosfull
PLATFORM_NAMES = timer runtime memory file keyboard divide joystick mouse video audio critical
OBJ_FILES = $(PLATFORM_ASM_DIR)/dosbss.obj $(addprefix $(C_DIR)/,$(RESTUNTS_OBJ_FILES)) $(OBJDIR)/pixldump.obj $(OBJDIR)/murmur3.obj $(OBJDIR)/legacy_context.obj $(OBJDIR)/pixlstart.obj $(PLATFORM_OBJECTS) $(PLATFORM_ASM_DIR)/dosstack.obj
START_OPTION = option START=_headless_start
LINK_ORDER = $(WATCOM_PORTED_LINK_ORDER)
CFLAGS = -dRESTUNTS_PIXLDUMP
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

$(OBJDIR)/%.obj: %.c $(wildcard ../c/*.h ../c/*/*.h) $(wildcard ../platform/dos/*.h) $(wildcard *.h) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WRAPPER_CFLAGS) $(CFLAGS) -i=../c -fo=$@ $<

$(OBJDIR)/pixlstart.obj: pixlstart.c ../platform/dos/dosstart.c $(wildcard ../platform/dos/*.h) $(wildcard ../c/*.h ../c/*/*.h) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_PLATFORM_CFLAGS) $(CFLAGS) -i=../c -fo=$@ $<


clean:
	$(call RMDIR,build/watcom)
	$(call REMOVE,*.exe)
	$(call REMOVE,*.map)
