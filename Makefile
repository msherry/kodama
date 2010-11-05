PORTAUDIODIR = portaudio

CC = gcc
LD = gcc

CFLAGS = -g -Wall -Wextra -DDEBUG=1
INCLUDES = -I${PORTAUDIODIR}/include
LDFLAGS = -L${PORTAUDIODIR}/lib/.libs
LIBRARIES = -lportaudio

OS := $(shell uname)
ifeq ($(OS), Linux)
	GLIB_INCLUDES = -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include
	GLIB_LIBS = -L/usr/lib -lgobject-2.0
else
	GLIB_INCLUDES := -I/opt/local/include/glib-2.0 -I/opt/local/lib/glib-2.0/include
	GLIB_LIBS := -L/opt/local/lib -lglib-2.0
endif

OBJS = cbuffer.o interface_hardware.o kodama.o

ALL: kodama

kodama: ${OBJS}
	${LD} -o kodama ${LDFLAGS} ${LIBRARIES} ${GLIB_LIBS} ${OBJS}

%.o: %.c
	${CC} ${CFLAGS} ${INCLUDES} ${GLIB_INCLUDES} -c $<

clean:
	rm -f *.o *.out kodama

check-syntax:
	${CC} ${CFLAGS} ${INCLUDES} ${GLIB_INCLUDES} -fsyntax-only $(CHK_SOURCES)

.PHONY: all clean check-syntax
