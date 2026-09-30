# 3DS homebrew Makefile: builds .elf, .3dsx and .cia.
# Art/map are generated first:  make assets   (needs ../tools/venv with pillow)
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM not set. Run: export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM")
endif
include $(DEVKITARM)/3ds_rules

TARGET   := grandtheftotto
BUILD    := build
SOURCES  := $(wildcard source/*.c)
APP_TITLE := Grand Theft Otto
APP_DESC  := Top-down LA crime as Otto
APP_AUTH  := ricozama
OBJS     := $(patsubst source/%.c,$(BUILD)/%.o,$(SOURCES)) $(BUILD)/atlas_t3x.o $(BUILD)/map_bin.o
GFX_PNG  := $(wildcard assets/gfx/*.png)
PY       := $(CURDIR)/../tools/venv/bin/python

# makerom / bannertool are built into ../tools/bin (see README)
TOOLS    := $(CURDIR)/../tools/bin
MAKEROM  := $(TOOLS)/makerom
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

# Regenerate art + map from the scripts in assets/
.PHONY: assets
assets:
	cd assets && $(PY) make_map.py && $(PY) make_art.py && $(PY) make_assets.py

# texture atlas (.t3x) and city map (.bin) -> object files linked into the game (no romfs needed)
$(BUILD)/atlas.t3x: assets/atlas.t3s $(GFX_PNG) | $(BUILD)
	tex3ds -i $< -o $@

$(BUILD)/map.bin: assets/map.bin | $(BUILD)
	cp $< $@

$(BUILD)/%_t3x.o $(BUILD)/%_t3x.h: $(BUILD)/%.t3x
	cd $(BUILD) && bin2s -a 4 -H $*_t3x.h $*.t3x | $(AS) -o $*_t3x.o

$(BUILD)/%_bin.o $(BUILD)/%_bin.h: $(BUILD)/%.bin
	cd $(BUILD) && bin2s -a 4 -H $*_bin.h $*.bin | $(AS) -o $*_bin.o

$(BUILD)/render.o: $(BUILD)/atlas_t3x.h
$(BUILD)/main.o: $(BUILD)/map_bin.h

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).smdh: assets/icon.png
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTH)" $< $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ --smdh=$(TARGET).smdh

$(BUILD)/banner.bnr: assets/banner.png assets/audio.wav | $(BUILD)
	$(BANNERTOOL) makebanner -i assets/banner.png -a assets/audio.wav -o $@

$(BUILD)/icon.icn: assets/icon.png | $(BUILD)
	$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESC)" -p "$(APP_AUTH)" -i assets/icon.png -o $@

$(TARGET).cia: $(TARGET).elf assets/app.rsf $(BUILD)/banner.bnr $(BUILD)/icon.icn
	$(MAKEROM) -f cia -o $@ -elf $(TARGET).elf -rsf assets/app.rsf \
	  -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t

-include $(OBJS:.o=.d)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).cia
