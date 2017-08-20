#
# Asteroids-3D makefile
#

CC ?= gcc
SRCDIR := src
BUILDDIR := build
TARGETDIR := bin
TARGET := $(TARGETDIR)/asteroids-3d

SRCEXT := c
SOURCES := asteroids-3d.c
OBJECTS := $(BUILDDIR)/asteroids-3d.o
DEBUGFLAGS := -Wall -Wextra -pedantic -Werror -Wfatal-errors -Wformat=2 \
	-Wswitch-enum -Wcast-align -Wpointer-arith -Wbad-function-cast \
	-Wno-strict-aliasing -Wstrict-overflow=5 -Wstrict-prototypes -Winline \
	-Wundef -Wnested-externs -Wcast-qual -Wshadow -Wunreachable-code \
	-Wlogical-op -Wfloat-equal -Wredundant-decls -Wold-style-definition \
	-ggdb3 -O0 -fno-omit-frame-pointer -ffloat-store -fno-common \
	-fstrict-aliasing
RELEASEFLAGS := -O2 -Wall -Wl,--strip-all
LIB := -lm -lSDL2 -lGL
INC := -Iinclude `sdl2-config --cflags`

all: | c89 debug-flag makedirs $(OBJECTS) $(TARGET)

debug-c89: | c89 debug-flag makedirs $(OBJECTS) $(TARGET)

release-c89: | c89 release-flag makedirs $(OBJECTS) $(TARGET)

debug-gnu89: | gnu89 debug-flag makedirs $(OBJECTS) $(TARGET)

release-gnu89: | gnu89 release-flag makedirs $(OBJECTS) $(TARGET)

$(TARGET): $(OBJECTS)
	@echo ""
	@echo " Linking..."
	@echo " $(CC) $(CFLAGS) $^ -o $(TARGET) $(LIB)" ; \
		$(CC) $(CFLAGS) $^ -o $(TARGET) $(LIB)

$(BUILDDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	@echo " $(CC) $(CFLAGS) $(INC) -c -o $@ $<"; \
		$(CC) $(CFLAGS) $(INC) -c -o $@ $<

makedirs:
	@mkdir -p $(BUILDDIR)
	@mkdir -p $(TARGETDIR)

debug-flag:
	$(eval CFLAGS += $(DEBUGFLAGS))

release-flag:
	$(eval CFLAGS += $(RELEASEFLAGS))

c89:
	$(eval DEBUGFLAGS="-std=c89" $(DEBUGFLAGS))
	$(eval RELEASEFLAGS="-std=c89" $(RELEASEFLAGS))

gnu89:
	$(eval DEBUGFLAGS="-std=gnu89" "-D_GNU_SOURCE" $(DEBUGFLAGS))
	$(eval RELEASEFLAGS="-std=gnu89" "-D_GNU_SOURCE" $(RELEASEFLAGS))

clean:
	@echo " Cleaning..."; 
	@echo " $(RM) -r $(BUILDDIR) $(TARGETDIR)" ; \
		$(RM) -r $(BUILDDIR) $(TARGETDIR)

.PHONY: all makedirs debug release clean tests
