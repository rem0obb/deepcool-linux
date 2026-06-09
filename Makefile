CC ?= cc
PKG_CONFIG ?= pkg-config

TARGET := deepcool-digital-linux
STATIC_TARGET := deepcool-digital-linux-static
CFLAGS ?= -O2 -Wall -Wextra -Wno-format-truncation -std=c11
CPPFLAGS += -D_GNU_SOURCE
PKGS := gtk4 hidapi-hidraw
LDLIBS += -ldl -lm
RESOURCE_XML := assets/deepcool.gresource.xml
RESOURCE_C := src/resources.c
RESOURCE_DEPS := $(RESOURCE_XML) assets/hicolor/index.theme assets/hicolor/256x256/apps/deepcool-digital-linux.png
SRCS := $(filter-out $(RESOURCE_C),$(shell find src -name '*.c' | sort)) $(RESOURCE_C)

PKG_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LIBS = $(shell $(PKG_CONFIG) --libs $(PKGS))
PKG_STATIC_LIBS = $(shell $(PKG_CONFIG) --static --libs $(PKGS))

all: $(TARGET)

check-deps:
	@command -v $(PKG_CONFIG) >/dev/null 2>&1 || { echo "Missing pkg-config. On Ubuntu: sudo apt install build-essential pkg-config libgtk-4-dev libhidapi-dev"; exit 1; }
	@command -v glib-compile-resources >/dev/null 2>&1 || { echo "Missing glib-compile-resources. On Ubuntu: sudo apt install libglib2.0-dev-bin"; exit 1; }
	@$(PKG_CONFIG) --exists $(PKGS) || { echo "Missing GTK4 or hidapi development files. On Ubuntu: sudo apt install libgtk-4-dev libhidapi-dev"; exit 1; }

check-static-deps: check-deps
	@test -f "$$($(PKG_CONFIG) --variable=libdir gtk4)/libgtk-4.a" || { echo "Static GTK4 archive not found. This distro/package set cannot build a fully static GTK binary."; exit 1; }
	@test -f "$$($(PKG_CONFIG) --variable=libdir pangocairo)/libpangocairo-1.0.a" || { echo "Static PangoCairo archive not found. Use a portable package format such as AppImage/Flatpak for releases."; exit 1; }

$(RESOURCE_C): $(RESOURCE_DEPS)
	glib-compile-resources --sourcedir=assets --target=$@ --generate-source $(RESOURCE_XML)

$(TARGET): check-deps $(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PKG_CFLAGS) -o $@ $(SRCS) $(PKG_LIBS) $(LDLIBS)

static: check-static-deps $(RESOURCE_C)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PKG_CFLAGS) -static -o $(STATIC_TARGET) $(SRCS) $(PKG_STATIC_LIBS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(STATIC_TARGET) $(RESOURCE_C)

.PHONY: all run static clean check-deps check-static-deps
