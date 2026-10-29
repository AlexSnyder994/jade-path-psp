TARGET = JadePath
OBJS = main.o

CFLAGS = -G0 -O2 -Wall -Wextra -Wno-unused-parameter
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
LIBDIR =
LIBS = -lpspgu -lpspgum -lpspctrl -lpspdisplay -lpspkernel -lm
LDFLAGS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Jade Path

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mk
