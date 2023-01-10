objs := retrogfx.o converter.o stb_image.o stb_image_write.o
outdir := debug
build := debug
CC := gcc
CXX := g++
CFLAGS := -I. -std=c11
CXXFLAGS := -I. -std=c++20 -Wall -Wextra
flags_deps = -MMD -MP -MF $(@:.o=.d)
# libs := -lfmt -lm
libs := -lfmt -L/usr/X11R6/lib -lm -lpthread -lX11 -lfmt

ifeq ($(build),debug)
    outdir := debug
    CFLAGS += -g -DDEBUG
    CXXFLAGS += -g -DDEBUG
else
    outdir := release
    CFLAGS += -O3
    CXXFLAGS += -O3
endif

_objs := $(patsubst %,$(outdir)/%,$(objs))

all: $(outdir)/chrconvert

$(outdir)/chrconvert: $(outdir) $(_objs) $(objs_convert)
	$(info Linking $@ ...)
	$(CXX) $(_objs) -o $@ $(libs)

$(outdir)/stb_image.o: src/stb_image.c
	$(info Compiling $< ...)
	@$(CC) $(CFLAGS) $(flags_deps) -c $< -o $@

$(outdir)/stb_image_write.o: src/stb_image_write.c
	$(info Compiling $< ...)
	@$(CC) $(CFLAGS) $(flags_deps) -c $< -o $@

$(outdir)/%.o: src/%.cpp
	$(info Compiling $< ...)
	@$(CXX) $(CXXFLAGS) $(flags_deps) -c $< -o $@

$(outdir):
	mkdir -p $(outdir)

.PHONY: clean

clean:
	rm -rf $(outdir)
