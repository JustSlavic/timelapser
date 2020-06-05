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
	video_renderer \
	logging \
	handler \


SOURCES := \
	frame \
	webcamera \
	video_renderer \
	logging \
	handler \


OBJECTS := $(addprefix build/$(SUB_DIR)/, $(addsuffix .o, $(SOURCES)))


# ==================================================================== #

.PHONY: all debug release prebuild postbuild clean


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
	@find build -type f -name '*.o' -delete
	@rm -f $(EXE_PATH)
	@rm -f run
	@echo "Cleaned"
