include ../watcom.mk

OBJDIR = build/watcom/$(CONFIG)
OUTFILE = $(OBJDIR)/DOSPLAT.EXE
PLATFORM_DIR = ../platform/dos/build/watcom/$(CONFIG)
PLATFORM_ASM_DIR = $(PLATFORM_DIR)/wasm
PLATFORM_NAMES = runtime memory file
PLATFORM_OBJECTS = $(PLATFORM_ASM_DIR)/dosbss.obj $(addprefix $(PLATFORM_DIR)/,$(addsuffix .obj,$(PLATFORM_NAMES))) $(PLATFORM_ASM_DIR)/dosstack.obj
OBJ_FILES = $(PLATFORM_ASM_DIR)/dosbss.obj $(OBJDIR)/dosstart.obj $(OBJDIR)/test-dos-platform.obj $(OBJDIR)/resource.obj $(filter-out $(PLATFORM_ASM_DIR)/dosbss.obj,$(PLATFORM_OBJECTS))
C_HEADERS = $(wildcard ../c/*.h) $(wildcard ../platform/dos/*.h)
SPACE := $(empty) $(empty)
COMMA := ,

.PHONY: all platform clean force-link
all: $(OUTFILE)

platform:
	$(MAKE) -C ../platform/dos CONFIG=$(CONFIG) LINKER=$(LINKER)

$(OBJDIR):
	$(call MKDIR,$@)

$(PLATFORM_OBJECTS): | platform

$(OBJDIR)/test-dos-platform.obj: test-dos-platform.c $(C_HEADERS) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_PLATFORM_CFLAGS) -i=../c -fo=$@ $<

$(OBJDIR)/dosstart.obj: ../platform/dos/dosstart.c $(C_HEADERS) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_PLATFORM_CFLAGS) -dRESTUNTS_HEADLESS -i=../c -fo=$@ $<

$(OBJDIR)/resource.obj: ../c/resource.c $(C_HEADERS) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_CFLAGS) -i=../c -fo=$@ $<

# Relink after the platform submake refreshes its objects.
force-link:

$(OUTFILE): $(OBJ_FILES) makefile ../watcom.mk force-link | platform
	$(file >$(OBJDIR)/link.lnk,name $(OUTFILE))
	$(file >>$(OBJDIR)/link.lnk,format dos $(WATCOM_LFLAGS) option START=_headless_start option MAP=$(OBJDIR)/dosplat.map)
	$(file >>$(OBJDIR)/link.lnk,$(WATCOM_LIBPATH))
	$(file >>$(OBJDIR)/link.lnk,file $(subst $(SPACE),$(COMMA),$(OBJ_FILES)))
	$(WLINK) @$(OBJDIR)/link.lnk

clean:
	$(call RMDIR,build/watcom)

# This test retains the complete game's objects and replaces only its entry module.
# Its extra test code therefore makes the memory budget slightly stricter.
include ../dos/full-game-objects.mk
FULL_GAME_DIR = ../c/build/watcom/$(CONFIG)/dosfull
RACE_MEMORY_OUTFILE = $(OBJDIR)/RACEMEM.EXE
RACE_MEMORY_PLATFORM_NAMES = timer runtime memory file keyboard divide joystick mouse video audio critical
RACE_MEMORY_OBJECTS = $(PLATFORM_ASM_DIR)/dosbss.obj $(subst $(FULL_GAME_DIR)/restunts.obj,$(OBJDIR)/test-race-memory.obj,$(addprefix $(FULL_GAME_DIR)/,$(RESTUNTS_OBJ_FILES))) $(addprefix $(PLATFORM_DIR)/,$(addsuffix .obj,$(RACE_MEMORY_PLATFORM_NAMES))) $(PLATFORM_ASM_DIR)/dosstack.obj

.PHONY: race-memory
race-memory: $(RACE_MEMORY_OUTFILE)

$(OBJDIR)/test-race-memory.obj: test-race-memory.c ../c/restunts.c $(C_HEADERS) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_CFLAGS) -dRESTUNTS_FULL -i=../c -fo=$@ $<

$(RACE_MEMORY_OUTFILE): $(RACE_MEMORY_OBJECTS) ../dos/full-game-objects.mk makefile ../watcom.mk force-link
	$(file >$(OBJDIR)/race-memory.lnk,name $(RACE_MEMORY_OUTFILE))
	$(file >>$(OBJDIR)/race-memory.lnk,format dos $(WATCOM_LFLAGS) option START=_headless_start option MAP=$(OBJDIR)/racemem.map)
	$(file >>$(OBJDIR)/race-memory.lnk,$(WATCOM_PORTED_LINK_ORDER))
	$(file >>$(OBJDIR)/race-memory.lnk,$(WATCOM_LIBPATH))
	$(file >>$(OBJDIR)/race-memory.lnk,file $(subst $(SPACE),$(COMMA),$(RACE_MEMORY_OBJECTS)))
	$(WLINK) @$(OBJDIR)/race-memory.lnk

GHOST_LINE_OUTFILE = $(OBJDIR)/GHOSTLIN.EXE
GHOST_LINE_OBJECTS = $(filter-out $(FULL_GAME_DIR)/ghost.obj,$(subst $(OBJDIR)/test-race-memory.obj,$(OBJDIR)/test-ghost-racing-line-dos.obj,$(RACE_MEMORY_OBJECTS)))

.PHONY: ghost-racing-line
ghost-racing-line: $(GHOST_LINE_OUTFILE)

$(OBJDIR)/test-ghost-racing-line-dos.obj: test-ghost-racing-line-dos.c ../c/restunts.c ../c/ghost.c $(C_HEADERS) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_CFLAGS) -fpc -zls -dRESTUNTS_FULL -i=../c -fo=$@ $<

$(GHOST_LINE_OUTFILE): $(GHOST_LINE_OBJECTS) ../dos/full-game-objects.mk makefile ../watcom.mk force-link
	$(file >$(OBJDIR)/ghost-line.lnk,name $(GHOST_LINE_OUTFILE))
	$(file >>$(OBJDIR)/ghost-line.lnk,format dos $(WATCOM_LFLAGS) option START=_headless_start option MAP=$(OBJDIR)/ghostlin.map)
	$(file >>$(OBJDIR)/ghost-line.lnk,$(WATCOM_PORTED_LINK_ORDER))
	$(file >>$(OBJDIR)/ghost-line.lnk,$(WATCOM_LIBPATH))
	$(file >>$(OBJDIR)/ghost-line.lnk,file $(subst $(SPACE),$(COMMA),$(GHOST_LINE_OBJECTS)))
	$(WLINK) @$(OBJDIR)/ghost-line.lnk
