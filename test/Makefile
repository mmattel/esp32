# Host tests for the NanoH2 DS18B20 Zigbee sketch.
#
#   make        build and run every suite
#   make clean  remove the binaries
#
# Each suite compiles the real sketch sources against local stub headers, so it
# always tests what is in NanoH2_DS18B20_Zigbee/ rather than a copy.

CXX ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1

SKETCH := ../NanoH2_DS18B20_Zigbee
SUITES := onewire slots zb_setting zb_link zb_mirror zb_version
BINARIES := $(addsuffix /run,$(SUITES))

.PHONY: all clean
all: $(BINARIES)
	@status=0; \
	for suite in $(SUITES); do \
	  echo "== $$suite"; \
	  ./$$suite/run || status=1; \
	  echo; \
	done; \
	exit $$status

onewire/run: onewire/test.cpp onewire/Arduino.h \
             $(SKETCH)/ds18b20_bus.cpp $(SKETCH)/ds18b20_bus.h
	$(CXX) $(CXXFLAGS) -Ionewire -o $@ $<

slots/run: slots/test.cpp slots/Arduino.h \
           $(SKETCH)/slots.cpp $(SKETCH)/slots.h $(SKETCH)/config.h $(SKETCH)/console.h
	$(CXX) $(CXXFLAGS) -Islots -o $@ $<

zb_setting/run: zb_setting/test.cpp zb_setting/Arduino.h zb_setting/Preferences.h zb_setting/Zigbee.h \
                $(SKETCH)/zb_setting.cpp $(SKETCH)/zb_setting.h $(SKETCH)/zb_version.h $(SKETCH)/config.h \
                $(SKETCH)/console.h
	$(CXX) $(CXXFLAGS) -Izb_setting -o $@ $<

zb_link/run: zb_link/test.cpp zb_link/Arduino.h zb_link/Zigbee.h \
             $(SKETCH)/zb_link.cpp $(SKETCH)/zb_link.h
	$(CXX) $(CXXFLAGS) -Izb_link -o $@ $<

zb_mirror/run: zb_mirror/test.cpp zb_mirror/Arduino.h zb_mirror/Zigbee.h \
               $(SKETCH)/zb_mirror.cpp $(SKETCH)/zb_mirror.h $(SKETCH)/config.h $(SKETCH)/console.h
	$(CXX) $(CXXFLAGS) -Izb_mirror -o $@ $<

zb_version/run: zb_version/test.cpp zb_version/Arduino.h zb_version/Zigbee.h \
                $(SKETCH)/zb_version.cpp $(SKETCH)/zb_version.h $(SKETCH)/config.h
	$(CXX) $(CXXFLAGS) -Izb_version -o $@ $<

clean:
	rm -f $(BINARIES)
