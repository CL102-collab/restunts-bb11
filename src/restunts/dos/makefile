include ../watcom.mk

include full-game-objects.mk

TARGET ?= full
ifeq (,$(filter $(TARGET),full original))
  $(error Unsupported game target: $(TARGET))
endif
OBJDIR = build/watcom/$(CONFIG)/$(TARGET)
ifeq ($(TARGET),original)
  OUTFILE = restunto.exe
  DESTFILE = restunto.exe
  include ../asmorig/link.mk
  OBJ_FILES = $(ASMORIG_LINK_OBJECTS) $(OBJDIR)/main.obj
  LINK_ORDER = $(ORIGINAL_LINK_ORDER)
  CFLAGS = -dRESTUNTS_ORIGINAL
  START_OPTION = $(ORIGINAL_LINK_OPTIONS)
else
  OUTFILE = restunts.exe
  DESTFILE = restunts.exe
  OBJ_FILES = $(PLATFORM_ASM_DIR)/dosbss.obj $(addprefix $(C_DIR)/,$(RESTUNTS_OBJ_FILES)) $(PLATFORM_OBJECTS) $(PLATFORM_ASM_DIR)/dosstack.obj
  START_OPTION = option START=_headless_start
  LINK_ORDER = $(WATCOM_PORTED_LINK_ORDER)
endif
C_DIR = ../c/build/watcom/$(CONFIG)/dosfull
PLATFORM_NAMES = timer runtime_options data_directory memory file keyboard divide joystick mouse video audio critical
PLATFORM_DIR = ../platform/dos/build/watcom/$(CONFIG)
PLATFORM_ASM_DIR = $(PLATFORM_DIR)/wasm
PLATFORM_OBJECTS = $(addprefix $(PLATFORM_DIR)/,$(addsuffix .obj,$(PLATFORM_NAMES)))
SPACE := $(empty) $(empty)
COMMA := ,

.PHONY: all clean
all: $(OBJDIR) $(OBJ_FILES) $(MAKEFILE_LIST)
	$(file >$(OBJDIR)/link.lnk,name $(OUTFILE))
	$(file >>$(OBJDIR)/link.lnk,format dos $(WATCOM_LFLAGS) $(START_OPTION) option MAP=$(basename $(OUTFILE)).map)
	$(file >>$(OBJDIR)/link.lnk,$(LINK_ORDER))
	$(file >>$(OBJDIR)/link.lnk,$(WATCOM_LIBPATH))
	$(file >>$(OBJDIR)/link.lnk,file $(subst $(SPACE),$(COMMA),$(OBJ_FILES)))
	$(WLINK) @$(OBJDIR)/link.lnk
	$(call COPY,$(OUTFILE),../../../stunts/$(DESTFILE))

$(OBJDIR):
	$(call MKDIR,$@)

$(OBJDIR)/%.obj: %.c $(wildcard ../c/*.h) $(wildcard *.h) makefile ../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_CFLAGS) $(CFLAGS) -i=../c -fo=$@ $<

clean:
	$(call RMDIR,build/watcom)
	$(call REMOVE,*.exe)
	$(call REMOVE,*.map)
