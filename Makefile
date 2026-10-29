TARGET = JadePath
OBJS = main.o

PSPSDK = $(shell psp-config --pspsdk-path)
PSPDEV = $(shell psp-config --pspdev-path)

CFLAGS = -G0 -O2 -Wall -Wextra -Wno-unused-parameter \
         -I$(PSPSDK)/include -I$(PSPDEV)/psp/include \
         -D_PSP_FW_VERSION=600

LDFLAGS = -L$(PSPSDK)/lib -L$(PSPDEV)/psp/lib
LIBS = -lpspdisplay -lpspkernel -lm

all: EBOOT.PBP

main.o: main.c
	psp-gcc $(CFLAGS) -c main.c -o main.o

$(TARGET).elf: main.o
	psp-gcc $(LDFLAGS) -o $@ main.o $(LIBS)
	psp-fixup-imports $@

PARAM.SFO:
	mksfoex -d MEMSIZE=1 'Jade Path' PARAM.SFO

EBOOT.PBP: $(TARGET).elf PARAM.SFO
	psp-strip $(TARGET).elf -o $(TARGET)_strip.elf
	pack-pbp EBOOT.PBP PARAM.SFO NULL NULL NULL NULL NULL $(TARGET)_strip.elf NULL
	-rm -f $(TARGET)_strip.elf

clean:
	-rm -f $(OBJS) $(TARGET).elf $(TARGET)_strip.elf EBOOT.PBP PARAM.SFO

.PHONY: all clean
