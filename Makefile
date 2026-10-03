# SkiFree port. Run inside `nix develop` (needs SDL2, pkg-config, python3+pefile).
#
#   make            build build/port/skifree (and the SDL-free build/port/skifree-sim)
#   make run        build and play
#   make test       headless checks (self-test, determinism, scenarios, screenshots)

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -std=c99
SDL_CFLAGS = $(shell pkg-config --cflags sdl2)
SDL_LIBS = $(shell pkg-config --libs sdl2)

OUT := build/port
EXE := etc/original-binaries/ski32.exe

all: $(OUT)/skifree $(OUT)/skifree-sim

$(OUT):
	mkdir -p $@

$(OUT)/assets.c: tools/gen_assets.py $(EXE) | $(OUT)
	python3 tools/gen_assets.py $(EXE) $@

$(OUT)/assets.o: $(OUT)/assets.c src/assets.h
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(OUT)/ski.o: src/ski.c src/ski.h src/assets.h src/platform.h | $(OUT)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(OUT)/headless.o: src/headless.c src/headless.h src/ski.h | $(OUT)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(OUT)/main_sdl.o: src/main_sdl.c src/ski.h src/assets.h src/platform.h src/font5x7.h src/headless.h | $(OUT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc -c $< -o $@

$(OUT)/sim.o: tests/sim.c src/headless.h src/platform.h src/ski.h | $(OUT)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(OUT)/skifree: $(OUT)/ski.o $(OUT)/headless.o $(OUT)/main_sdl.o $(OUT)/assets.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(SDL_LIBS) -lm -o $@

# The game core without SDL: deterministic tests, sanitizer builds.
$(OUT)/skifree-sim: $(OUT)/ski.o $(OUT)/headless.o $(OUT)/sim.o $(OUT)/assets.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@

run: $(OUT)/skifree
	$(OUT)/skifree

test: all
	tests/run.sh $(OUT)

# Memory and UB checks: 2 x 30000 ticks of random play on the SDL-free
# build. Built with Apple's clang in a clean environment because, on this
# macOS, ASan binaries built inside the Nix environment hang at startup.
SAN := build/port-asan
SAN_ENV := env -i HOME="$(HOME)" PATH=/usr/bin:/bin
SAN_CFLAGS := -O1 -g -std=c99 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
sanitize: $(OUT)/assets.c
	mkdir -p $(SAN) && cp $(OUT)/assets.c $(SAN)/assets.c
	$(SAN_ENV) /usr/bin/make -s OUT=$(SAN) CC=/usr/bin/clang CFLAGS="$(SAN_CFLAGS)" $(SAN)/skifree-sim
	$(SAN_ENV) tests/fuzz.sh $(SAN)/skifree-sim 42
	$(SAN_ENV) tests/fuzz.sh $(SAN)/skifree-sim 7

clean:
	rm -rf $(OUT) $(SAN)

.PHONY: all run test sanitize clean
