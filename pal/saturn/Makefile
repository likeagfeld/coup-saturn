#
# Makefile - Saturn PAL Build Configuration
#
# This Makefile builds the Saturn platform abstraction layer using Jo Engine.
# It follows the standard Jo Engine project structure.
#

# Project configuration
PROJECT_NAME = cui_saturn_pal
TARGET_NAME = cui_saturn

# Jo Engine SDK root directory (the cloned joengine repo)
# Override with: make JOENGINE_ROOT=/path/to/joengine
JOENGINE_ROOT ?= /opt/joengine

# Jo Engine paths (as expected by jo_engine_makefile)
JO_ENGINE_SRC_DIR = $(JOENGINE_ROOT)/jo_engine
COMPILER_DIR = $(JOENGINE_ROOT)/Compiler

# Jo Engine configuration
# Set JO_COMPILE_USING_SGL=0 to use Jo Engine's renderer (not SGL)
JO_COMPILE_USING_SGL = 1
JO_COMPILE_WITH_VIDEO_MODULE = 0
JO_COMPILE_WITH_BACKUP_MODULE = 0
JO_COMPILE_WITH_TGA_MODULE = 0
JO_COMPILE_WITH_AUDIO_MODULE = 0
JO_COMPILE_WITH_3D_MODULE = 0
JO_DEBUG = 1

# Extra compiler flags - add cui core include path
_CCFLAGS = -I../../core/include

# Source files
SRCS = saturn_pal.c

# Include Jo Engine build rules
-include $(COMPILER_DIR)/COMMON/jo_engine_makefile

# If Jo Engine Makefile doesn't exist, provide fallback
ifeq ($(wildcard $(COMPILER_DIR)/COMMON/jo_engine_makefile),)
$(warning Jo Engine not found at $(JOENGINE_ROOT))
$(warning Expected: $(COMPILER_DIR)/COMMON/jo_engine_makefile)

.PHONY: help
help:
	@echo "Saturn PAL Build System"
	@echo ""
	@echo "Jo Engine not found. Please install Jo Engine:"
	@echo "  1. Clone from https://github.com/johannes-fetz/joengine.git"
	@echo "  2. Set JOENGINE_ROOT to the joengine root directory"
	@echo ""
	@echo "Example:"
	@echo "  git clone https://github.com/johannes-fetz/joengine.git ~/Projects/joengine"
	@echo "  export JOENGINE_ROOT=~/Projects/joengine"
	@echo "  make"

.DEFAULT_GOAL := help
endif

# Custom targets

.PHONY: clean-all
clean-all: clean
	@echo "Cleaned Saturn PAL build artifacts"

.PHONY: info
info:
	@echo "Saturn PAL Build Configuration"
	@echo "  Project:        $(PROJECT_NAME)"
	@echo "  Target:         $(TARGET_NAME)"
	@echo "  Jo Engine:      $(JO_ENGINE_SRC_DIR)"
	@echo "  Use SGL:        $(JO_COMPILE_USING_SGL)"
	@echo "  Sources:        $(SRCS)"
	@echo "  Extra CFLAGS:   $(EXTRA_CFLAGS)"

# Development targets

.PHONY: test-mednafen
test-mednafen: all
	@echo "Launching in Mednafen emulator..."
	@if [ -f "$(TARGET_NAME).cue" ]; then \
		mednafen -force_module ss "$(TARGET_NAME).cue"; \
	elif [ -f "$(TARGET_NAME).iso" ]; then \
		mednafen -force_module ss "$(TARGET_NAME).iso"; \
	else \
		echo "Error: No disc image found ($(TARGET_NAME).cue or .iso)"; \
		exit 1; \
	fi

.PHONY: test-yabause
test-yabause: all
	@echo "Launching in Yabause emulator..."
	@if [ -f "$(TARGET_NAME).cue" ]; then \
		yabause -i "$(TARGET_NAME).cue"; \
	elif [ -f "$(TARGET_NAME).iso" ]; then \
		yabause -i "$(TARGET_NAME).iso"; \
	else \
		echo "Error: No disc image found"; \
		exit 1; \
	fi

# Notes for developers:
#
# Jo Engine Makefile.common typically provides these targets:
#   make          - Build disc image
#   make re       - Clean and rebuild
#   make clean    - Remove build artifacts
#   make run      - Build and run in emulator
#
# Build output:
#   *.bin, *.cue  - Disc image files for emulators
#   *.iso         - Alternative disc image format
#
# Supported emulators:
#   - Mednafen (recommended, accurate)
#   - Yabause
#   - SSF
#   - Real hardware via Satiator/Rhea/Phoebe
