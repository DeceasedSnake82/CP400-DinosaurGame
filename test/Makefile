# Host-side tests (run on your PC / in CI, not on the calculator).
#
#   export SDK_DIR=/sdk         # same SDK the calculator build uses (headers only are needed)
#   make -C test game           # rules: physics, obstacles, fairness (bot plays 100k+ ticks)
#   make -C test shots          # run the real UI headlessly, write screenshots to test/out/
#   make -C test perf           # host-side frame timings
#
SDK_DIR ?= /sdk
CXX     ?= g++
OUT     := out
STD     := -std=gnu++17

GAME_SRC := ../game.cpp ../shapes.cpp
APP_SRC  := host_app.cpp ../main.cpp ../gfx.cpp ../render.cpp $(GAME_SRC)
APP_FLAGS := -O2 -Wall -Wextra $(STD) -DDINO_HOST -Dmain=dino_main -I$(SDK_DIR)/include -I..

.PHONY: test game render shots perf clean

test: game render

game: $(OUT)/t_game
	$(OUT)/t_game

render: $(OUT)/t_render
	$(OUT)/t_render

$(OUT)/t_game: test_game.cpp $(GAME_SRC) $(wildcard ../*.h) | $(OUT)
	$(CXX) -O2 -Wall -Wextra $(STD) -I.. -o $@ test_game.cpp $(GAME_SRC)

$(OUT)/t_render: test_render.cpp ../render.cpp ../gfx.cpp $(GAME_SRC) $(wildcard ../*.h) | $(OUT)
	$(CXX) -O2 -Wall -Wextra $(STD) -DDINO_HOST -I$(SDK_DIR)/include -I.. -o $@ test_render.cpp ../render.cpp ../gfx.cpp $(GAME_SRC)

$(OUT)/app: $(APP_SRC) $(wildcard ../*.h) | $(OUT)
	$(CXX) $(APP_FLAGS) -o $@ $(APP_SRC)

shots: $(OUT)/app
	$(OUT)/app script.txt
	python3 ppm2png.py $(OUT)

perf: $(OUT)/app
	$(OUT)/app perf.txt

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)
