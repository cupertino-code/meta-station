PROG = crsf-bridge

CFLAGS += -O2 -g

all: ${PROG}

${PROG}: crsf-bridge.o circ_buf.o

crsf-bridge.o: crsf-bridge.c circ_buf.h crsf_protocol.h

circ_buf.o: circ_buf.c circ_buf.h

update: crsf-bridge
	scp crsf-bridge ant:

clean:
	-rm *.o ${PROG} 
