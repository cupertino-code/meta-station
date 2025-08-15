LDLIBS = -lpthread -lgpiod -lm -lcairo
CFLAGS = -pedantic -Wall -Wextra -O2 -g

all: station antenna

station: station.o crc8.o visualisation.o

station.o: station.c protocol.h common.h

visualisation.o: visualisation.c visualisation.h

crc8.o: crc8.c protocol.h

antenna: antenna.o crc8.o

antenna.o: antenna.c protocol.h common.h

clean:
	-rm -f station station.o crc8.o antenna.o
