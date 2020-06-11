PROJECT := timelapser

CXX := g++

CXX_STANDARD := c++14

INC_DIR := \
	/usr/include \
	src \

LIB_DIR := \
	/usr/lib \

LIBS := \
	avutil \
	avformat \
	avcodec \

CXXFLAGS := \
	-Wall \
	-std=$(CXX_STANDARD)

CXXFLAGS += $(addprefix -I, $(INC_DIR))

LDFLAGS := \
	$(addprefix -L, $(LIB_DIR)) \
	$(addprefix -l, $(LIBS))


ifndef MAKECMDGOALS
	SUB_DIR  := debug
	CXXFLAGS += -ggdb3
	CXXFLAGS += -D_DEBUG
else ifeq ($(MAKECMDGOALS),debug)
	SUB_DIR  := debug
	CXXFLAGS += -ggdb3
	CXXFLAGS += -D_DEBUG
else ifeq ($(MAKECMDGOALS),release)
	SUB_DIR  := release
	CXXFLAGS += -O2 -D_RELEASE
endif

EXE_PATH := bin/$(SUB_DIR)/$(PROJECT)



HEADERS := \
	frame \
	webcamera \
	video_encoder \
	logging \
	handler \


SOURCES := \
	frame \
	webcamera \
	video_encoder \
	logging \
	handler \


OBJECTS := $(addprefix build/$(SUB_DIR)/, $(addsuffix .o, $(SOURCES)))


# ==================================================================== #

.PHONY: all debug release prebuild postbuild clean encoder mwe muxing


all: debug

debug: prebuild $(EXE_PATH) postbuild

release: prebuild $(EXE_PATH) postbuild

prebuild:
	@mkdir -p bin/$(SUB_DIR)
	@mkdir -p build/$(SUB_DIR)

$(EXE_PATH): main.cpp $(OBJECTS)
	g++ main.cpp $(OBJECTS) -o $(EXE_PATH) $(CXXFLAGS) $(LDFLAGS)

build/$(SUB_DIR)/%.o: src/%.cpp src/%.h
	g++ $< -c -o $@ $(CXXFLAGS)

postbuild:
	@ln -sfn $(EXE_PATH) run


clean:
	@rm -rf bin/debug/$(PROJECT)
	@rm -rf build/debug/*.o
	@rm -rf bin/release/$(PROJECT)
	@rm -rf build/release/*.o
	@rm -rf data/*
	@rm -f run
	@echo "Cleaned"

encoder:
	gcc encoder.c -o encoder -I/usr/include -L/usr/lib -lavutil -lavformat -lavcodec

mwe:
	gcc mwe.c -o mwe -ggdb3 -Wall -I/usr/include -L/usr/lib -lavutil -lavformat -lavcodec

muxing:
	gcc muxing.c -o muxing -ggdb3 -Wall -I/usr/include -L/usr/lib -lavutil -lavformat -lavcodec -lm -lswscale -lswresample

