# Depends on glib and gnet (not installed by default)

PORTAUDIODIR = portaudio

CC = gcc
LD = gcc

PEDANTIC = -pedantic -fstrict-aliasing -Wno-variadic-macros -Wno-declaration-after-statement -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align -fno-common
OPTFLAGS = #-O3 -ftree-vectorize -ftree-vectorizer-verbose=5 -ffast-math -msse4.1

CFLAGS = -g ${PEDANTIC} ${OPTFLAGS} -Wall -Wextra -DDEBUG=1 -std=gnu99 -save-temps
INCLUDES = -I${PORTAUDIODIR}/include
LDFLAGS = -L${PORTAUDIODIR}/lib/.libs
LIBRARIES = -lportaudio -lm

OS := $(shell uname)
ifeq ($(OS), Linux)
	GLIB_INCLUDES = -I/usr/include/glib-2.0 \
	-I/usr/include/gnet-2.0 \
	-I/usr/lib/glib-2.0/include \
	-I/usr/lib/gnet-2.0/include
	GLIB_LIBS = -L/usr/lib -lgobject-2.0 -lgnet-2.0
else
	GLIB_INCLUDES = -I/opt/local/include/glib-2.0 \
	-I/opt/local/include/gnet-2.0 \
	-I/opt/local/lib/glib-2.0/include \
	-I/opt/local/lib/gnet-2.0/include
	GLIB_LIBS = -L/opt/local/lib -lglib-2.0 -lgnet-2.0
endif

OBJS = cbuffer.o echo.o hybrid.o iir.o interface_hardware.o \
	interface_tcp.o interface_udp.o kodama.o protocol.o

ALL: kodama

kodama: ${OBJS}
	${LD} -o kodama ${LDFLAGS} ${LIBRARIES} ${GLIB_LIBS} ${OBJS}

-include ${OBJS:.o=.d}

%.o: %.c
	${CC} ${CFLAGS} ${INCLUDES} ${GLIB_INCLUDES} -c $<
	${CC} ${CFLAGS} -MM $< > $*.d

clean:
	rm -f *.o *.s *.i *.out *.d *flymake* kodama

check-syntax:
	${CC} ${CFLAGS} ${INCLUDES} ${GLIB_INCLUDES} -fsyntax-only $(CHK_SOURCES)

.PHONY: all clean check-syntax
