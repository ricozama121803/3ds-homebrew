# 3DS homebrew Makefile: builds .elf, .3dsx and .cia.
# Textures are generated first:  make assets   (needs ../tools/venv with pillow + numpy)
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM not set. Run: export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM")
endif
include $(DEVKITARM)/3ds_rules

TARGET   := sunsetops
BUILD    := build
SOURCES  := $(wildcard source/*.c)
APP_TITLE := Sunset Ops
APP_DESC  := First-person team deathmatch
APP_AUTH  := ricozama
TEXNAMES := white asphalt deck concrete panel window stucco container wood roof grass sand glow cloud chars
MIPPED   := asphalt deck concrete panel window stucco container wood roof grass sand
OBJS     := $(patsubst source/%.c,$(BUILD)/%.o,$(SOURCES)) $(BUILD)/textures_bin.o $(BUILD)/sunset_shbin.o
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

.PHONY: all 3dsx cia clean assets
all: 3dsx cia
3dsx: $(TARGET).3dsx
cia: $(TARGET).cia

$(BUILD)/%.o: source/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

# Regenerate textures, icon and banner from the scripts in assets/
assets:
	cd assets && $(PY) make_textures.py && $(PY) make_art.py

# one .t3x per texture (mipmapped when it is tiled), packed into one blob that is linked into the game (no romfs needed)
$(BUILD)/tex_%.t3x: assets/gfx/%.png | $(BUILD)
	tex3ds -f rgba $(if $(filter $*,$(MIPPED)),-m box,) -z auto -o $@ $<

$(BUILD)/textures.bin: $(addprefix $(BUILD)/tex_,$(addsuffix .t3x,$(TEXNAMES))) assets/pack_textures.py
	$(PY) assets/pack_textures.py $@ $(addprefix $(BUILD)/tex_,$(addsuffix .t3x,$(TEXNAMES)))

$(BUILD)/sunset.shbin: source/sunset.v.pica | $(BUILD)
	picasso -o $@ $<

$(BUILD)/%_shbin.o $(BUILD)/%_shbin.h: $(BUILD)/%.shbin
	cd $(BUILD) && bin2s -a 4 -H $*_shbin.h $*.shbin | $(AS) -o $*_shbin.o

$(BUILD)/%_bin.o $(BUILD)/%_bin.h: $(BUILD)/%.bin
	cd $(BUILD) && bin2s -a 4 -H $*_bin.h $*.bin | $(AS) -o $*_bin.o

$(BUILD)/gfx3ds.o: $(BUILD)/sunset_shbin.h $(BUILD)/textures_bin.h

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
