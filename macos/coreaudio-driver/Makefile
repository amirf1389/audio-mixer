# Audio Mixer.driver: a Core Audio AudioServerPlugIn. Build on a Mac with the Xcode command line tools:  make
# Install (administrator): sudo make install   then   sudo launchctl kickstart -k system/com.apple.audio.coreaudiod
# (443ABAB8-... in Info.plist is Apple's AudioServerPlugIn type id; 8C5C2B6A-... is this driver's factory id.)
CC      ?= clang
ARCHS   ?= -arch arm64 -arch x86_64
BUNDLE   = "Audio Mixer.driver"
HALDIR   = /Library/Audio/Plug-Ins/HAL

all: $(BUNDLE)/Contents/MacOS/AudioMixer

$(BUNDLE)/Contents/MacOS/AudioMixer: AudioMixerDriver.c ../../drivers/common/am_link.c ../../drivers/common/am_link.h Info.plist
	mkdir -p $(BUNDLE)/Contents/MacOS
	cp Info.plist $(BUNDLE)/Contents/Info.plist
	$(CC) $(ARCHS) -std=gnu99 -O2 -Wall -bundle -fvisibility=hidden -o $@ AudioMixerDriver.c ../../drivers/common/am_link.c -framework CoreAudio -framework CoreFoundation
	codesign --force --sign - $(BUNDLE) || true

install: all
	sudo rm -rf $(HALDIR)/$(BUNDLE)
	sudo cp -R $(BUNDLE) $(HALDIR)/
	sudo launchctl kickstart -k system/com.apple.audio.coreaudiod

uninstall:
	sudo rm -rf $(HALDIR)/$(BUNDLE)
	sudo launchctl kickstart -k system/com.apple.audio.coreaudiod

clean:
	rm -rf $(BUNDLE)

.PHONY: all install uninstall clean
