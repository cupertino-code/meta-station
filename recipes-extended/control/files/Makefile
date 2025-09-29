LDLIBS = -lpthread -lgpiod -lm -lcairo
CFLAGS = -pedantic -Wall -Wextra -O2 -g

CFLAGS += $$(pkg-config --cflags libmisc)
LDLIBS += $$(pkg-config --libs libmisc)

all: station antenna

station: station.o visualisation.o

station.o: station.c protocol.h common.h

visualisation.o: visualisation.c visualisation.h

antenna: antenna.o

antenna.o: antenna.c protocol.h common.h

clean:
	-rm -f station antenna station.o utils.o antenna.o visualisation.o
