# pio neni v PATH, kdyz je PlatformIO jen VSCode extension - zije v
# ~/.platformio/penv/bin/pio. Pouzije se z PATH, pokud tam je, jinak tahle
# plna cesta.
PIO ?= $(shell command -v pio 2>/dev/null || echo $(HOME)/.platformio/penv/bin/pio)

# "pio run -t upload" NE - esptool 4.5.1 v PlatformIO na tehle desce pada na
# BrokenPipeError, viz ../FLASH.md. Flashuje se samostatnym flash.sh.
FLASH_SH := ../flash.sh

.DEFAULT_GOAL := build

.PHONY: build flash check monitor clean

build:
	$(PIO) run

flash: build
	$(FLASH_SH) -d .

check:
	$(FLASH_SH) -d . --check

monitor:
	$(PIO) device monitor

clean:
	$(PIO) run -t clean
