# Bolt, Chord and Sphere - a screensaver of the three things an avout owns, from Neal Stephenson's "Anathem"
VERSION  := 1.0.0
PREFIX   ?= /usr
SYSCONFDIR ?= /etc
DESTDIR  ?=
CC       ?= cc
# gnu11 keeps the binary free of very recent glibc symbols, so packages built on a
# new distribution still run on older ones
CFLAGS   ?= -O2 -g
override CFLAGS += -std=gnu11 -Wall -Isrc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

MINGW    ?= x86_64-w64-mingw32
MINGW_ROOT ?= /usr/$(MINGW)/sys-root/mingw

all: build/bolt-chord-sphere

# the pictures in assets/art are built into the program, so it must be compiled from here
build/bolt-chord-sphere: src/bcs.c src/stb_image.h $(wildcard assets/art/*)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -o $@ src/bcs.c $(LDFLAGS) $(SDL_LIBS) -lm

check: build/bolt-chord-sphere
	SDL_VIDEODRIVER=dummy ./build/bolt-chord-sphere --snapshot build/check.bmp --time 110 --size 640x400 && echo "ran a full round"

install: all
	install -Dm755 build/bolt-chord-sphere $(DESTDIR)$(PREFIX)/bin/bolt-chord-sphere
	install -Dm755 packaging/linux/bolt-chord-sphere-screensaver $(DESTDIR)$(PREFIX)/bin/bolt-chord-sphere-screensaver
	install -Dm644 packaging/linux/bolt-chord-sphere.desktop $(DESTDIR)$(PREFIX)/share/applications/bolt-chord-sphere.desktop
	install -Dm644 packaging/linux/bolt-chord-sphere-autostart.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/bolt-chord-sphere-screensaver.desktop
	install -Dm644 packaging/bolt-chord-sphere.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/bolt-chord-sphere.png
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/bolt-chord-sphere/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/bolt-chord-sphere/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/bolt-chord-sphere $(DESTDIR)$(PREFIX)/bin/bolt-chord-sphere-screensaver
	rm -f $(DESTDIR)$(PREFIX)/share/applications/bolt-chord-sphere.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/bolt-chord-sphere-screensaver.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/bolt-chord-sphere.png
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/bolt-chord-sphere

# ---- packages (written to dist/) ----
ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

deb: all
	rm -rf build/deb && mkdir -p build/deb/DEBIAN dist
	$(MAKE) install DESTDIR=build/deb PREFIX=/usr
	strip build/deb/usr/bin/bolt-chord-sphere
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(ARCH_DEB)/' \
	    -e "s/@SIZE@/$$(du -sk build/deb/usr | cut -f1)/" packaging/linux/control.in > build/deb/DEBIAN/control
	echo /etc/xdg/autostart/bolt-chord-sphere-screensaver.desktop > build/deb/DEBIAN/conffiles
	dpkg-deb --root-owner-group --build build/deb dist/bolt-chord-sphere_$(VERSION)_$(ARCH_DEB).deb

rpm:
	mkdir -p dist build/rpm/SOURCES
	git archive --format=tar.gz --prefix=bolt-chord-sphere-$(VERSION)/ -o build/rpm/SOURCES/bolt-chord-sphere-$(VERSION).tar.gz HEAD
	rpmbuild -bb $(RPMFLAGS) --define "_topdir $(CURDIR)/build/rpm" --define "version $(VERSION)" packaging/linux/bolt-chord-sphere.spec
	cp build/rpm/RPMS/*/bolt-chord-sphere-$(VERSION)-*.rpm dist/

# Windows: the screensaver itself, and a small BoltChordSphere.scr that goes in the
# Windows system folder (so it is listed with the other screensavers) and starts it
windows:
	@mkdir -p build/win
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Isrc -I$(MINGW_ROOT)/include/SDL2 -Dmain=SDL_main \
	    -o build/win/bolt-chord-sphere.exe src/bcs.c \
	    -L$(MINGW_ROOT)/lib -lmingw32 -lSDL2main -lSDL2 -lm -mwindows -static-libgcc
	$(MINGW)-windres packaging/windows/stub.rc -O coff -o build/win/stub.res
	$(MINGW)-gcc -O2 -Wall -o build/win/BoltChordSphere.scr packaging/windows/stub.c build/win/stub.res -mwindows -static
	$(MINGW)-strip build/win/bolt-chord-sphere.exe build/win/BoltChordSphere.scr
	cp $(MINGW_ROOT)/bin/SDL2.dll build/win/
	[ ! -f $(MINGW_ROOT)/bin/SDL3.dll ] || cp $(MINGW_ROOT)/bin/SDL3.dll build/win/

installer: windows
	mkdir -p dist
	makensis -V2 -DVERSION=$(VERSION) packaging/windows/bolt-chord-sphere.nsi

dist: deb rpm installer

clean:
	rm -rf build dist

.PHONY: all check install uninstall deb rpm windows installer dist clean
