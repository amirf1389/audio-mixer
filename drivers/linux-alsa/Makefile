# ALSA plugin "audiomixer" (virtual sound card for Audio Mixer). Needs the ALSA development files: libasound2-dev (Debian / Ubuntu), alsa-lib-devel (Fedora).
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PLUGIN   = libasound_module_pcm_audiomixer.so
LIBDIR  := $(shell pkg-config --variable=libdir alsa 2>/dev/null || echo /usr/lib)
ALSADIR ?= $(LIBDIR)/alsa-lib

all: $(PLUGIN)

$(PLUGIN): pcm_audiomixer.c ../common/am_link.c ../common/am_link.h
	$(CC) -std=gnu99 $(CFLAGS) -fPIC -DPIC -shared -o $@ pcm_audiomixer.c ../common/am_link.c -lasound -lpthread

# sudo make install: copies the plugin where ALSA looks for plugins; asound.conf.example shows the device definition (put it in /etc/asound.conf or ~/.asoundrc)
install: $(PLUGIN)
	install -d $(DESTDIR)$(ALSADIR)
	install -m 755 $(PLUGIN) $(DESTDIR)$(ALSADIR)/$(PLUGIN)

clean:
	rm -f $(PLUGIN)

.PHONY: all install clean
