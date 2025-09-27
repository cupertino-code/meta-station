PROG = crsf-bridge

CFLAGS += -O2 -g -MMD
CFLAGS += $$(pkg-config --cflags libmisc)
LDLIBS += $$(pkg-config --libs libmisc)

DEPS = $(OBJ:.o=.d)

all: ${PROG}

-include $(DEPS)

${PROG}: crsf-bridge.o circ_buf.o

crsf-bridge.o: crsf-bridge.c circ_buf.h

circ_buf.o: circ_buf.c circ_buf.h

update: crsf-bridge
	scp crsf-bridge ant:

clean:
	-rm *.o ${PROG} 
