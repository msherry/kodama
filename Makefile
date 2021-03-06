# Depends on glib and gnet (not installed by default)

PORTAUDIODIR = portaudio

#/opt/local/bin/gcc-mp-4.6

CC=gcc
LD=gcc

PEDANTIC = -pedantic -fstrict-aliasing -Wno-variadic-macros -Wno-declaration-after-statement -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align -fno-common -Wfloat-equal -Wno-system-headers -Wundef
ARCH_FLAGS = -msse4.1
OPTFLAGS = -O3 -ftree-vectorize -ftree-vectorizer-verbose=5 -ffast-math
LINKTIME_OPTFLAGS =
PROFILE_FLAGS = -pg

CFLAGS = -g ${PROFILE_FLAGS} ${ARCH_FLAGS} ${OPTFLAGS} -Wall \
	-Wextra ${PEDANTIC} -std=gnu99 -fverbose-asm \
	-DDEBUG=1 -D_FILE_OFFSET_BITS=64 -DG_ERRORCHECK_MUTEXES -DFAST_DOTP \
	-DFAST_GEIGEL_DTD

INCLUDES = -I${PORTAUDIODIR}/include -I/usr/local/include
LDFLAGS = ${PROFILE_FLAGS} -L${PORTAUDIODIR}/lib/.libs
LIBRARIES = -lportaudio -lm -lavcodec -lavformat -lavutil -lavcore

OS := $(shell uname)
ifeq ($(OS), Linux)
	GLIB_INCLUDES = -I/usr/include/glib-2.0 \
	-I/usr/include/gnet-2.0 \
	-I/usr/lib/glib-2.0/include \
	-I/usr/lib/gnet-2.0/include
	GLIB_LIBS = -L/usr/lib -lgobject-2.0 -lgnet-2.0
	ARCH_FLAGS += -mtune=barcelona
else
	# This one can tune for corei7
	CC=/opt/local/bin/gcc-mp-4.6
	GLIB_INCLUDES = -I/opt/local/include/glib-2.0 \
	-I/opt/local/include/gnet-2.0 \
	-I/opt/local/lib/glib-2.0/include \
	-I/opt/local/lib/gnet-2.0/include
        # need to bring in lgthread explicitly on the Mac
	GLIB_LIBS = -L/opt/local/lib -lglib-2.0 -lgnet-2.0 -lgthread-2.0

	ARCH_FLAGS += -mtune=corei7 # corei7-avx, when available
	#Introduced in 4.6, but not supported on Mac os yet
	PEDANTIC += -fno-var-tracking
	# TODO: Look into the -fsplit-stack option
	OPTFLAGS += -flto
	LINKTIME_OPTFLAGS += -flto -fwhole-program
endif

OBJS = av.o calibrate.o cbuffer.o conversation.o echo.o hybrid.o flv.o iir.o \
	imolist.o imo_message.o interface_hardware.o interface_tcp.o \
	interface_udp.o kodama.o protocol.o read_write.o util.o

PROG = kodama

ALL: ${PROG} documentation

${PROG}: ${OBJS}
	${LD} -o ${PROG} ${LDFLAGS} ${LINKTIME_OPTFLAGS} ${LIBRARIES} ${GLIB_LIBS} ${OBJS}

-include ${OBJS:.o=.d}

%.o: %.c
	${CC} ${CFLAGS} ${INCLUDES} ${GLIB_INCLUDES} -c $<
	${CC} ${CFLAGS} ${INCLUDES} ${GLIB_INCLUDES} -S $<
	${CC} ${CFLAGS} ${INCLUDES} -MM $< > $*.d

documentation:
	doxygen Doxyfile || true # don't let this kill us

clean:
	rm -f *.o *.s *.i *.out *.d *flymake* ${PROG}

distclean: clean
	rm -rf docs

check-syntax:
	${CC} ${CFLAGS} ${INCLUDES} ${GLIB_INCLUDES} -fsyntax-only $(CHK_SOURCES)
	rats $(CHK_SOURCES)

.PHONY: all clean check-syntax
