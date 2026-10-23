#---------------------------------------------------------------------------------
# TOWER DEFENSE - Wii homebrew (devkitPPC + libogc)
#
#   make        builds boot.dol (open it in Dolphin, or copy it to apps/towerdefense/ on an SD card)
#   make clean
#   make test   native test harness (needs only gcc + python3; no Wii toolchain)
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC is not set. Install devkitPro (https://devkitpro.org/wiki/Getting_Started) and run:  sudo dkp-pacman -S wii-dev   then   export DEVKITPPC=/opt/devkitpro/devkitPPC")
endif

include $(DEVKITPPC)/wii_rules
export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITPPC)/bin:$(PATH)

TARGET  := boot
SRCDIR  := source
OBJS    := $(SRCDIR)/main.o $(SRCDIR)/game.o $(SRCDIR)/audio.o

CFLAGS  := -O2 -Wall -std=gnu11 $(MACHDEP) -I$(LIBOGC_INC) -I$(SRCDIR)
LDFLAGS := $(MACHDEP) -L$(LIBOGC_LIB)
LIBS    := -lasnd -lwiiuse -lbte -logc -lm

all: $(TARGET).dol

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

$(OBJS): %.o: %.c $(SRCDIR)/game.h $(SRCDIR)/audio.h $(SRCDIR)/art.inc
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).dol

test:
	sh tools/run_tests.sh

.PHONY: all clean test
