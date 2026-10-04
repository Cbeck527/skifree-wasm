# SkiFree port. Run inside `nix develop` (needs SDL2, pkg-config, python3+pefile).
#
#   make            build build/port/skifree (and the SDL-free build/port/skifree-sim)
#   make run        build and play
#   make test       headless checks (self-test, determinism, scenarios, screenshots)
#   make web        browser build in build/web (emscripten); make serve to play it
#   make web-test   check the core compiled to wasm plays the same games as native

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -std=c99
SDL_CFLAGS = $(shell pkg-config --cflags sdl2)
SDL_LIBS = $(shell pkg-config --libs sdl2)

OUT := build/port
EXE := etc/original-binaries/ski32.exe
EXE_URL := https://ski.ihoc.net/ski32.exe
EXE_SHA256 := 3572d3757638bc1ae388c3d007ba59ba1f370911901e2a35d2f1297e8cf0ff35

all: $(OUT)/skifree $(OUT)/skifree-sim

$(OUT):
	mkdir -p $@

# The original isn't redistributed here, so fetch it from its author's site.
$(EXE):
	mkdir -p $(@D)
	curl -fsSL -o $@.tmp $(EXE_URL)
	python3 -c 'import hashlib, sys; h = hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest(); sys.exit(h != sys.argv[2] and f"{sys.argv[1]}: sha256 {h}, expected {sys.argv[2]}")' $@.tmp $(EXE_SHA256) \
		|| { rm -f $@.tmp; exit 1; }
	mv $@.tmp $@

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

# Browser build: build/web/index.html, index.js and index.wasm, the whole
# deployable site. emscripten fetches and builds its SDL2 port into its cache
# on first use. Browsers won't load .wasm from file://, hence `make serve`.
WEB := build/web
EMCC := emcc
WEB_CFLAGS := -O2 -Wall -Wextra -std=c99
WEB_SRC := src/ski.c src/headless.c src/main_sdl.c $(OUT)/assets.c
CORE_H := src/ski.h src/assets.h src/platform.h src/headless.h

web: $(WEB)/index.html

$(WEB)/index.html: $(WEB_SRC) $(CORE_H) src/font5x7.h web/shell.html
	mkdir -p $(WEB)
	$(EMCC) $(WEB_CFLAGS) -sUSE_SDL=2 -sENVIRONMENT=web -Isrc $(WEB_SRC) \
		--shell-file web/shell.html -o $@

serve: web
	python3 -m http.server -d $(WEB) 8000

# The SDL-free core compiled to wasm, run under node by a wrapper script so
# it takes the same command lines as the native skifree-sim.
WEBSIM := build/web-sim
$(WEBSIM)/skifree-sim: src/ski.c src/headless.c tests/sim.c $(OUT)/assets.c $(CORE_H)
	mkdir -p $(WEBSIM)
	$(EMCC) $(WEB_CFLAGS) -sENVIRONMENT=node -sEXIT_RUNTIME=1 -Isrc \
		src/ski.c src/headless.c tests/sim.c $(OUT)/assets.c -o $@.js
	printf '#!/bin/sh\nexec node "$$(dirname "$$0")/skifree-sim.js" "$$@"\n' > $@
	chmod +x $@

web-test: $(OUT)/skifree-sim $(WEBSIM)/skifree-sim
	tests/web.sh $(OUT)/skifree-sim $(WEBSIM)/skifree-sim

clean:
	rm -rf $(OUT) $(SAN) $(WEB) $(WEBSIM)

.PHONY: all run test sanitize web serve web-test clean
