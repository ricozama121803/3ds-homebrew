# BeatPad: builds .elf, .3dsx and .cia; songs live in romfs/ (generate them with tools/make_song.py)
# `make cia` also works without devkitPro once the .elf exists (it only needs makerom + bannertool)
ifeq ($(strip $(DEVKITARM)),)
ifneq ($(MAKECMDGOALS),cia)
$(error "DEVKITARM not set. Run: source ../env.sh  (or ../dkp-make.sh beatpad to build in the devkitARM container)")
endif
else
include $(DEVKITARM)/3ds_rules
endif

TARGET    := beatpad
BUILD     := build
SOURCES   := $(wildcard source/*.c)
APP_TITLE := BeatPad
APP_DESC  := Button rhythm game
APP_AUTH  := ricozama
OBJS      := $(patsubst source/%.c,$(BUILD)/%.o,$(SOURCES))
ROMFS     := romfs
ROMFS_FILES := $(shell find $(ROMFS) -type f 2>/dev/null)

TOOLS      := $(CURDIR)/../tools/bin
MAKEROM    := $(TOOLS)/makerom
BANNERTOOL := $(TOOLS)/bannertool

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) \
            -D__3DS__ -I$(CTRULIB)/include -I$(BUILD) -MMD -MP
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib
LIBS     := -lcitro2d -lcitro3d -lctru -lm

.PHONY: all 3dsx cia clean
all: 3dsx cia
3dsx: $(TARGET).3dsx
cia: $(TARGET).cia

$(BUILD)/%.o: source/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

ifneq ($(strip $(DEVKITARM)),)
$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@
endif

$(TARGET).smdh: assets/icon.png
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTH)" $< $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh $(ROMFS_FILES)
	3dsxtool $< $@ --smdh=$(TARGET).smdh --romfs=$(ROMFS)

$(BUILD)/banner.bnr: assets/banner.png assets/audio.wav | $(BUILD)
	$(BANNERTOOL) makebanner -i assets/banner.png -a assets/audio.wav -o $@

$(BUILD)/icon.icn: assets/icon.png | $(BUILD)
	$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESC)" -p "$(APP_AUTH)" -i assets/icon.png -o $@

$(TARGET).cia: $(TARGET).elf assets/app.rsf $(BUILD)/banner.bnr $(BUILD)/icon.icn $(ROMFS_FILES)
	$(MAKEROM) -f cia -o $@ -elf $(TARGET).elf -rsf assets/app.rsf \
	  -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t

ifneq ($(strip $(DEVKITARM)),)
-include $(OBJS:.o=.d)        # (skipped for a host-only `make cia`: the .d files name container paths)
endif

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).cia
