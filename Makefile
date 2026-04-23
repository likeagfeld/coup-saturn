# Coup - Saturn + browser crossplay card game
#
# Top-level targets:
#   coup-all      Build everything (Saturn binary, Python server with .so, web client when wasm lands)
#   coup-lib      Host build of libcoup_rules (dylib/so) for local server dev
#   coup-server   Dockerized gcc:14 build of libcoup_rules.so + Python server package
#   coup-saturn   Dockerized Saturn binary (invokes scripts/docker-saturn-build.sh)
#   test-coup     Host-toolchain unit tests (uses tests/framework + tests/coup)
#   clean         rm -rf build/
#
# Single source of truth for shared-library sources: COUP_LIB_SRCS below.
# build.sh delegates to these targets; do not duplicate source lists elsewhere.

CC ?= gcc
AR ?= ar

# Directories
BUILD_DIR := build
CORE_SRC := core/src
CORE_INC := core/include
TESTS_DIR := tests
TESTS_FW := tests/framework

# Compiler flags
CFLAGS := -Wall -Wextra -Werror -std=c99 -pedantic
CFLAGS += -I$(CORE_INC)

# Debug/Release
ifdef DEBUG
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# === Coup source groups (single source of truth) ===
COUP_SRC_DIR := examples/coup
COUP_RULES_SRCS := $(COUP_SRC_DIR)/coup_rules.c \
                   $(COUP_SRC_DIR)/coup_event_log.c
COUP_BRIDGE_SRCS := $(COUP_SRC_DIR)/coup_rules_bridge.c \
                    $(COUP_SRC_DIR)/coup_bot_bridge.c
COUP_BOT_SRCS := $(COUP_SRC_DIR)/coup_bot.c
COUP_GAME_SRCS := $(COUP_SRC_DIR)/coup_game.c
COUP_VIEW_SRCS := $(COUP_SRC_DIR)/coup_table_view.c

# Shared library for Python server (coup rule engine + bot)
COUP_LIB_SRCS := $(COUP_RULES_SRCS) $(COUP_BRIDGE_SRCS) $(COUP_BOT_SRCS) $(COUP_VIEW_SRCS)
COUP_LIB_OBJS := $(patsubst %.c,$(BUILD_DIR)/coup-lib/%.o,$(COUP_LIB_SRCS))

# Python server package sources
COUP_SERVER_SRC := tools/coup_server
COUP_SERVER_OUT := $(BUILD_DIR)/coup_server
COUP_SERVER_SRCS := $(COUP_SERVER_SRC)/server.py \
                    $(COUP_SERVER_SRC)/coup_engine.py \
                    $(COUP_SERVER_SRC)/bot_player.py

# Saturn build
COUP_SATURN_DIR := examples/coup/saturn
COUP_GAME_OUT := $(BUILD_DIR)/coup_game

# Platform-dependent shared library extension
ifeq ($(shell uname -s),Darwin)
    COUP_SHARED_LIB := $(BUILD_DIR)/libcoup_rules.dylib
    SHARED_FLAGS := -dynamiclib
else
    COUP_SHARED_LIB := $(BUILD_DIR)/libcoup_rules.so
    SHARED_FLAGS := -shared
endif

# === Top-level targets ===

.PHONY: all
all: coup-all

.PHONY: coup-all
coup-all: coup-lib coup-server coup-saturn

# --- coup-lib: host toolchain shared library for local server dev ---
.PHONY: coup-lib
coup-lib: $(COUP_SHARED_LIB)

$(COUP_SHARED_LIB): $(COUP_LIB_OBJS)
	$(CC) $(SHARED_FLAGS) -o $@ $^
	@echo "Built: $@"

$(BUILD_DIR)/coup-lib/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -Wall -Wextra -std=c99 -O2 -fPIC -Iexamples/coup -c -o $@ $<

# --- coup-server: Dockerized Linux build + Python package ---
.PHONY: coup-server
coup-server:
	@echo "Building coup server (Linux x86_64)..."
	@rm -rf $(COUP_SERVER_OUT)
	@mkdir -p $(COUP_SERVER_OUT)
	@docker run --rm --platform linux/amd64 -v "$(shell pwd)":/src -w /src gcc:14 \
		sh -c "gcc -Wall -Wextra -std=c99 -O2 -fPIC -shared \
			-Iexamples/coup \
			-o $(COUP_SERVER_OUT)/libcoup_rules.so \
			$(COUP_LIB_SRCS)"
	@docker run --rm --platform linux/amd64 -v "$(shell pwd)":/src gcc:14 \
		sh -c "nm -D /src/$(COUP_SERVER_OUT)/libcoup_rules.so | grep -q bridge_init"
	@cp $(COUP_SERVER_SRCS) $(COUP_SERVER_OUT)/
	@echo "Built: $(COUP_SERVER_OUT)/"

# --- coup-saturn: Dockerized Saturn disc image ---
.PHONY: coup-saturn
coup-saturn:
	@echo "Building Saturn coup disc image..."
	@if [ -f $(COUP_SRC_DIR)/assets/rebellion.mp3 ] && [ ! -f $(COUP_SATURN_DIR)/rebellion.wav ]; then \
		if command -v ffmpeg >/dev/null 2>&1; then \
			echo "Converting rebellion.mp3 -> rebellion.wav..."; \
			ffmpeg -y -i $(COUP_SRC_DIR)/assets/rebellion.mp3 -ar 44100 -ac 2 -sample_fmt s16 $(COUP_SATURN_DIR)/rebellion.wav 2>/dev/null; \
		else \
			echo "Warning: ffmpeg not found; CD-DA track will be missing"; \
		fi; \
	fi
	./scripts/docker-saturn-build.sh $(COUP_SATURN_DIR) ""
	@rm -rf $(COUP_GAME_OUT)
	@mkdir -p $(COUP_GAME_OUT)
	@cp $(COUP_SATURN_DIR)/_build/game.cue $(COUP_GAME_OUT)/
	@cp $(COUP_SATURN_DIR)/_build/track01.bin $(COUP_GAME_OUT)/
	@if [ -f $(COUP_SATURN_DIR)/rebellion.wav ]; then \
		cp $(COUP_SATURN_DIR)/rebellion.wav $(COUP_GAME_OUT)/; \
	fi
	@echo "Built: $(COUP_GAME_OUT)/"

# === Coup tests ===
COUP_TEST_DIR := tests/coup
COUP_TEST_SRCS := $(wildcard $(COUP_TEST_DIR)/*.c) \
                  $(COUP_GAME_SRCS) $(COUP_RULES_SRCS) $(COUP_BOT_SRCS) $(COUP_VIEW_SRCS) \
                  $(TESTS_FW)/cui_test_framework.c \
                  $(TESTS_FW)/mocks/mock_pal.c \
                  $(CORE_SRC)/cui_pal.c
COUP_TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(COUP_TEST_SRCS))
COUP_TEST_RUNNER := $(BUILD_DIR)/test_coup_runner

.PHONY: test-coup
test-coup: $(COUP_TEST_RUNNER)
	@echo "Running Coup tests..."
	@./$(COUP_TEST_RUNNER)

$(COUP_TEST_RUNNER): $(COUP_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Built: $@"

# Coup test compilation (needs framework + coup source + mock includes)
$(BUILD_DIR)/$(COUP_TEST_DIR)/%.o: $(COUP_TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(TESTS_FW) -I$(COUP_SRC_DIR) -I$(TESTS_FW)/mocks -c -o $@ $<

# Coup source compilation
$(BUILD_DIR)/$(COUP_SRC_DIR)/%.o: $(COUP_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(COUP_SRC_DIR) -c -o $@ $<

# Framework/mocks/core compilation used by test-coup
$(BUILD_DIR)/$(CORE_SRC)/%.o: $(CORE_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/$(TESTS_FW)/%.o: $(TESTS_FW)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(TESTS_FW) -I$(TESTS_FW)/mocks -c -o $@ $<

# === Clean ===
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned build directory"

# === Help ===
.PHONY: help
help:
	@echo "Coup Makefile targets:"
	@echo "  make coup-all    - Build Saturn binary + server + host lib"
	@echo "  make coup-lib    - Host build of libcoup_rules (dylib/so)"
	@echo "  make coup-server - Dockerized gcc:14 build + Python server package"
	@echo "  make coup-saturn - Dockerized Saturn disc image (game.cue/track01.bin)"
	@echo "  make test-coup   - Host-toolchain unit tests"
	@echo "  make clean       - Remove build artifacts"
	@echo "  make all         - Alias for coup-all"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1          - Build with debug symbols"
