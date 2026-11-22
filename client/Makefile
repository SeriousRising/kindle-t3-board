CC      ?= gcc
ARM_CC  ?= arm-linux-gnueabi-gcc
CFLAGS  := -O2 -Wall -Wextra -std=gnu99 -D_POSIX_C_SOURCE=200809L
SRCS    := main.c fb_epdc.c http_client.c input_touch.c ui.c

.PHONY: all host kindle fonts clean

all: host

fonts: font_atlas.h

font_atlas.h: gen_fonts.py
	python3 gen_fonts.py

host: font_atlas.h $(SRCS)
	$(CC) $(CFLAGS) -o t3board $(SRCS)

# Cross-compile ARMv7 binary for Kindle Touch (D01200 / i.MX50 / Linux 2.6.31 / glibc 2.12)
# using koreader/koxtoolchain's `kindle5` target (`arm-kindle5-linux-gnueabi-gcc`).
kindle: font_atlas.h $(SRCS)
	$(ARM_CC) $(CFLAGS) -march=armv7-a -mtune=cortex-a8 -mfloat-abi=softfp -mfpu=neon -o t3board-kindle $(SRCS) -lrt

clean:
	rm -f t3board t3board-kindle *.o
