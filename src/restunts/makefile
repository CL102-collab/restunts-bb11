include watcom.mk

MAKE_PARAMS = CONFIG=$(CONFIG) LINKER=$(LINKER)
.DEFAULT_GOAL := restunts
.PHONY: restunts restunts-original asmorig dosplatform c-headless c-full repldump repldump-original pixldump pixldump-original test-dos-platform test-race-memory test-ghost-racing-line test-driving-options clean

restunts: dosplatform c-full
	$(MAKE) -C dos TARGET=full $(MAKE_PARAMS)

restunts-original: asmorig
	$(MAKE) -C dos TARGET=original $(MAKE_PARAMS)

asmorig:
	$(MAKE) -C asmorig $(MAKE_PARAMS)

dosplatform:
	$(MAKE) -C platform/dos $(MAKE_PARAMS)

c-headless:
	$(MAKE) -C c TARGET=dosheadless $(MAKE_PARAMS)

c-full:
	$(MAKE) -C c TARGET=dosfull $(MAKE_PARAMS)

repldump: dosplatform c-headless
	$(MAKE) -C repldump TARGET=headless $(MAKE_PARAMS)

repldump-original: asmorig dosplatform
	$(MAKE) -C repldump TARGET=original $(MAKE_PARAMS)

pixldump: dosplatform c-full
	$(MAKE) -C pixldump TARGET=ported $(MAKE_PARAMS)

pixldump-original: asmorig dosplatform
	$(MAKE) -C pixldump TARGET=original $(MAKE_PARAMS)

test-dos-platform:
	$(MAKE) -C tests $(MAKE_PARAMS)

test-race-memory: dosplatform c-full
	$(MAKE) -C tests race-memory $(MAKE_PARAMS)

test-ghost-racing-line: dosplatform c-full
	$(MAKE) -C tests ghost-racing-line $(MAKE_PARAMS)

test-driving-options: dosplatform c-full
	$(MAKE) -C tests driving-options startup-parser $(MAKE_PARAMS)

clean:
	$(MAKE) -C dos $(MAKE_PARAMS) clean
	$(MAKE) -C asmorig $(MAKE_PARAMS) clean
	$(MAKE) -C platform/dos $(MAKE_PARAMS) clean
	$(MAKE) -C c TARGET=dosheadless $(MAKE_PARAMS) clean
	$(MAKE) -C c TARGET=dosfull $(MAKE_PARAMS) clean
	$(MAKE) -C repldump $(MAKE_PARAMS) clean
	$(MAKE) -C pixldump $(MAKE_PARAMS) clean
	$(MAKE) -C tests $(MAKE_PARAMS) clean
