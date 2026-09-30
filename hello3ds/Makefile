# Minimal 3DS homebrew Makefile: builds .elf, .3dsx and .cia
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM not set. Run: export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM")
endif
include $(DEVKITARM)/3ds_rules

TARGET   := hello3ds
BUILD    := build
SOURCES  := $(wildcard source/*.c)
OBJS     := $(patsubst source/%.c,$(BUILD)/%.o,$(SOURCES))

# makerom / bannertool are built into ../tools/bin (see README)
TOOLS    := $(CURDIR)/../tools/bin
MAKEROM  := $(TOOLS)/makerom
BANNERTOOL := $(TOOLS)/bannertool

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) \
            -D__3DS__ -I$(CTRULIB)/include
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib
LIBS     := -lctru -lm

.PHONY: all 3dsx cia clean
all: 3dsx cia
3dsx: $(TARGET).3dsx
cia: $(TARGET).cia

$(BUILD)/%.o: source/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).smdh: assets/icon.png
	smdhtool --create "Hello3DS" "My first 3DS game" "ricozama" $< $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ --smdh=$(TARGET).smdh

$(BUILD)/banner.bnr: assets/banner.png assets/audio.wav | $(BUILD)
	$(BANNERTOOL) makebanner -i assets/banner.png -a assets/audio.wav -o $@

$(BUILD)/icon.icn: assets/icon.png | $(BUILD)
	$(BANNERTOOL) makesmdh -s "Hello3DS" -l "My first 3DS game" -p "ricozama" -i assets/icon.png -o $@

$(TARGET).cia: $(TARGET).elf assets/app.rsf $(BUILD)/banner.bnr $(BUILD)/icon.icn
	$(MAKEROM) -f cia -o $@ -elf $(TARGET).elf -rsf assets/app.rsf \
	  -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -exefslogo -target t

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).cia
