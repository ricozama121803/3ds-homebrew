# 3DS homebrew Makefile: builds .elf, .3dsx and .cia.
# Home Menu art is generated from PC preview renders:  make assets   (needs ../tools/venv with pillow)
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM not set. Run: source ../env.sh")
endif
include $(DEVKITARM)/3ds_rules

TARGET   := ringside
BUILD    := build
SOURCES  := $(wildcard source/*.c)
APP_TITLE := Ringside
APP_DESC  := 3D kickboxing
APP_AUTH  := ricozama
OBJS     := $(patsubst source/%.c,$(BUILD)/%.o,$(SOURCES)) $(BUILD)/shader_shbin.o
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

.PHONY: all 3dsx cia clean assets preview
all: 3dsx cia
3dsx: $(TARGET).3dsx
cia: $(TARGET).cia

$(BUILD)/%.o: source/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

# Home Menu icon/banner/jingle (icon.png, banner.png, audio.wav) from the PC preview renderer
assets:
	cd assets && $(PY) make_assets.py

# PC preview renders (software rasteriser): see host/preview.sh
preview:
	host/preview.sh /tmp/ringside-preview hi

# vertex shader (.v.pica) -> .shbin -> object file linked into the game (no romfs needed)
$(BUILD)/shader.shbin: source/shader.v.pica | $(BUILD)
	picasso -o $@ $<

$(BUILD)/%_shbin.o $(BUILD)/%_shbin.h: $(BUILD)/%.shbin
	cd $(BUILD) && bin2s -a 4 -H $*_shbin.h $*.shbin | $(AS) -o $*_shbin.o

$(BUILD)/gfx_3ds.o: $(BUILD)/shader_shbin.h

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
