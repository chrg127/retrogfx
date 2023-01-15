# objs := retrogfx.o converter.o stb_image.o stb_image_write.o
# outdir := debug
# build := debug

CFLAGS := -I. -std=c11
CC := gcc
CXX := g++
CXXFLAGS := -I../lib -std=c++20 -Wall -Wextra \
			-Wno-missing-field-initializers # needed for warnings on stb_image_write
LDLIBS := -lfmt -lm

all: converter

converter: ../lib/retrogfx.cpp converter.cpp

clean:
	rm converter

.PHONY: clean
