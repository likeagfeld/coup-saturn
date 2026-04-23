#
# Makefile - Saturn Coup Card Game
#
# 2-6 player online bluffing card game using NetLink modem.
# Shared game logic in examples/coup/, Saturn-specific code here.
#

# Compiler paths
JOENGINE_ROOT ?= /opt/joengine
COMPILER_DIR = $(JOENGINE_ROOT)/Compiler

ifeq ($(OS), Windows_NT)
	CC = $(COMPILER_DIR)/WINDOWS/bin/sh-elf-gcc.exe
	CONV = $(COMPILER_DIR)/WINDOWS/bin/sh-elf-objcopy.exe
else
	CC = $(COMPILER_DIR)/LINUX/bin/sh-none-elf-gcc-8.2.0
	CONV = $(COMPILER_DIR)/LINUX/bin/sh-none-elf-objcopy
endif

# Paths
CMNDIR = $(COMPILER_DIR)/COMMON
SGLDIR = $(CMNDIR)/SGL_302j

# cui paths
CUI_ROOT := ../../..
CORE_INC := $(CUI_ROOT)/core/include
CORE_SRC := $(CUI_ROOT)/core/src
PAL_SATURN := $(CUI_ROOT)/pal/saturn
COUP_SHARED := ..

# Build output subdirectory (keeps .o and intermediates out of source tree)
BUILD = _build

# Resolution define passed from parent makefile (e.g., -DSATURN_RES_640)
SATURN_RES_DEFINE ?=

# Compiler flags
CCFLAGS = -m2 -O2 -fomit-frame-pointer \
	-Wall -Wextra -Wno-unused-parameter \
	-fno-common -fno-builtin \
	-nostdlib -nodefaultlibs \
	-I. \
	-I$(COUP_SHARED) \
	-I$(CORE_INC) \
	-I$(PAL_SATURN) \
	-I$(COMPILER_DIR)/WINDOWS/sh-elf/include \
	-I$(SGLDIR)/INC \
	-D__SATURN__ -DSATURN \
	$(SATURN_RES_DEFINE)

# Linker flags
LDFLAGS = -m2 -nostartfiles \
	-L$(SGLDIR)/LIB_ELF \
	-Xlinker -T$(CMNDIR)/sgl.linker \
	-Xlinker -Map -Xlinker $(BUILD)/game.map \
	-Xlinker -e -Xlinker ___Start

# Startup object (must link first)
SYSOBJS = $(SGLDIR)/LIB_ELF/SGLAREA.O

# Libraries (link after user objects)
LIBS = $(SGLDIR)/LIB_ELF/SEGA_SYS.A \
	$(SGLDIR)/LIB_ELF/LIBSGL.A \
	$(SGLDIR)/LIB_ELF/LIBCD.A

# Source files - Saturn entry point
SRCS = main_saturn.c
SRCS += coup_sprite_loader.c
SRCS += coup_gameover_loader.c
SRCS += coup_anim_loader.c

# Shared coup sources (keep in sync with Makefile COUP_*_SRCS)
SRCS += $(COUP_SHARED)/coup_game.c
SRCS += $(COUP_SHARED)/coup_render.c
SRCS += $(COUP_SHARED)/coup_audio.c
SRCS += $(COUP_SHARED)/coup_rules.c
SRCS += $(COUP_SHARED)/coup_table_view.c
SRCS += $(COUP_SHARED)/coup_bot.c

# cui core sources
SRCS += $(CORE_SRC)/cui_pal.c
SRCS += $(CORE_SRC)/cui_theme.c
SRCS += $(CORE_SRC)/cui_layout.c
SRCS += $(CORE_SRC)/cui_color_mapper.c

# Saturn PAL sources
SRCS += $(PAL_SATURN)/saturn_storage.c
SRCS += $(PAL_SATURN)/saturn_pal.c
SRCS += $(PAL_SATURN)/saturn_layout.c
SRCS += $(PAL_SATURN)/saturn_color_mapper.c
SRCS += $(PAL_SATURN)/saturn_input.c
SRCS += $(PAL_SATURN)/saturn_key_repeat.c
SRCS += $(PAL_SATURN)/saturn_vdp2.c
SRCS += $(PAL_SATURN)/saturn_font.c
SRCS += $(PAL_SATURN)/saturn_vdp1.c

# Object files in build subdirectory
OBJS = $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o)))

# Tell make where to find .c files (all basenames are unique)
vpath %.c . $(COUP_SHARED) $(CORE_SRC) $(PAL_SATURN)

# Targets
TARGET = $(BUILD)/game.elf
TARGET_RAW = $(BUILD)/game.raw
TARGET_ISO = $(BUILD)/track01.bin
TARGET_CUE = $(BUILD)/game.cue

# Tools
MKISOFS = mkisofs
RM = rm -rf

# CD-DA audio track (pre-converted from assets/rebellion.mp3 via ffmpeg)
# Run on host: ffmpeg -y -i ../assets/rebellion.mp3 -ar 44100 -ac 2 -sample_fmt s16 rebellion.wav
MUSIC_WAV = rebellion.wav

.PHONY: all clean help

all: $(TARGET_CUE)

# Single pattern rule for all objects
$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	$(CC) $(CCFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(BUILD)
	$(CC) $(LDFLAGS) $(SYSOBJS) $(OBJS) $(LIBS) -lgcc -o $@

$(TARGET_RAW): $(TARGET)
	@mkdir -p $(BUILD)/cd
	$(CONV) -O binary $(TARGET) $(TARGET_RAW)
	$(CONV) -O binary $(TARGET) $(BUILD)/cd/0.bin

$(TARGET_ISO): $(TARGET_RAW)
	$(MKISOFS) -quiet -sysid "SEGA SATURN" -volid "COUP" \
		-volset "COUP" -sectype 2352 \
		-publisher "SEGA ENTERPRISES, LTD." \
		-preparer "SEGA ENTERPRISES, LTD." \
		-appid "COUP" \
		-generic-boot $(CMNDIR)/IP.BIN \
		-full-iso9660-filenames -o $(TARGET_ISO) $(BUILD)/cd

$(TARGET_CUE): $(TARGET_ISO) $(MUSIC_WAV)
	@echo 'FILE "track01.bin" BINARY' > $(TARGET_CUE)
	@echo '  TRACK 01 MODE1/2048' >> $(TARGET_CUE)
	@echo '    INDEX 01 00:00:00' >> $(TARGET_CUE)
	@echo '    POSTGAP 00:02:00' >> $(TARGET_CUE)
	@echo 'FILE "rebellion.wav" WAVE' >> $(TARGET_CUE)
	@echo '  TRACK 02 AUDIO' >> $(TARGET_CUE)
	@echo '    INDEX 01 00:00:00' >> $(TARGET_CUE)
	@echo "Generated $(TARGET_CUE)"

clean:
	$(RM) $(BUILD)

help:
	@echo "Coup Card Game (Saturn NetLink Edition)"
	@echo ""
	@echo "2-6 player online bluffing card game."
	@echo "Game logic in ../coup_game.c, Saturn entry point here."
	@echo "Server: tools/coup_server/server.py"
	@echo ""
	@echo "Targets:"
	@echo "  all   - Build game.cue/iso"
	@echo "  clean - Remove build artifacts"
