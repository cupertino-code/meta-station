LDLIBS = -lpthread -lgpiod -lm -lcairo
CFLAGS = -pedantic -Wall -Wextra -O2 -g

all: station antenna

station: station.o utils.o visualisation.o

station.o: station.c protocol.h common.h

visualisation.o: visualisation.c visualisation.h

utils.o: utils.c common.h

antenna: antenna.o utils.o

antenna.o: antenna.c protocol.h common.h

clean:
	-rm -f station antenna station.o utils.o antenna.o visualisation.o
