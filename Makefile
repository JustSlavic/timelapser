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



HEADERS = \
	frame \
	webcamera \
	video_renderer \
	logging \
	handler \


SOURCES = \
	frame \
	webcamera \
	video_renderer \
	logging \
	handler \


HEADERS      := $(addprefix src/,     $(addsuffix .h,   $(HEADERS)))
OBJECTS      := $(addprefix build/,   $(addsuffix .o,   $(SOURCES)))
SOURCES      := $(addprefix src/,     $(addsuffix .cpp, $(SOURCES)))


# ==================================================================== #

.PHONY: set_debug debug prebuild postbuild clean


all: prebuild bin/$(PROJECT) bin/vr postbuild

debug: set_debug all

set_debug:
	$(eval CXXFLAGS += -ggdb3)


bin/$(PROJECT): main.cpp $(OBJECTS)
	g++ main.cpp $(OBJECTS) -O0 -o bin/$(PROJECT) $(CXXFLAGS) $(LDFLAGS)


bin/vr: video_reader.cpp
	g++ video_reader.cpp -o bin/vr $(CXXFLAGS) $(LDFLAGS)


build/%.o: src/%.cpp src/%.h
	g++ $< -c -o $@ $(CXXFLAGS)


prebuild:
	@mkdir -p bin
	@mkdir -p build


postbuild:
	@ln -sfn bin/$(PROJECT) run


clean:
	find build -type f -name '*.o' -delete
	rm -f bin/$(PROJECT)
	rm -f bin/vr
