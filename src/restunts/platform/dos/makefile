include ../../watcom.mk
OBJDIR = build/watcom/$(CONFIG)
ASMOBJDIR = $(OBJDIR)/wasm
ASM = $(WASM) $(WASM_AFLAGS)
C_HEADERS = $(wildcard ../../c/*.h) $(wildcard *.h)

.PHONY: all clean
all: $(ASMOBJDIR)/dosbss.obj $(ASMOBJDIR)/dosstack.obj $(OBJDIR)/timer.obj $(OBJDIR)/runtime.obj $(OBJDIR)/runtime_options.obj $(OBJDIR)/data_directory.obj $(OBJDIR)/memory.obj $(OBJDIR)/file.obj $(OBJDIR)/keyboard.obj $(OBJDIR)/divide.obj $(OBJDIR)/joystick.obj $(OBJDIR)/mouse.obj $(OBJDIR)/video.obj $(OBJDIR)/audio.obj $(OBJDIR)/critical.obj

$(OBJDIR) $(ASMOBJDIR):
	$(call MKDIR,$@)

$(ASMOBJDIR)/%.obj: %.asm makefile ../../watcom.mk $(WATCOM_STAMP) | $(ASMOBJDIR)
	$(ASM) -fo="$@" $<

# The original dump wrappers have a fixed segment budget. Keep their runtime
# unchanged and link startup-directory cleanup only into the ported full game.
$(OBJDIR)/runtime_options.obj: runtime.c $(C_HEADERS) makefile ../../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_PLATFORM_CFLAGS) -dRESTUNTS_STARTUP_DIRECTORY -i=../../c -fo=$@ $<

$(OBJDIR)/%.obj: %.c $(C_HEADERS) makefile ../../watcom.mk $(WATCOM_STAMP) | $(OBJDIR)
	$(CC) $(WATCOM_PLATFORM_CFLAGS) -i=../../c -fo=$@ $<

clean:
	$(call RMDIR,$(OBJDIR))
