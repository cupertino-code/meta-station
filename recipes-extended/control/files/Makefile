VIDEO_WIDTH ?= 720
VIDEO_HEIGHT ?= 576

LDLIBS += -lpthread -lgpiod -lm -lcairo -lstdc++ -lyaml-cpp
FLAGS = -Wall -Wextra -O2 -g -DVIDEO_WIDTH=${VIDEO_WIDTH} -DVIDEO_HEIGHT=${VIDEO_HEIGHT}

FLAGS += $$(pkg-config --cflags libmisc)
CFLAGS += $(FLAGS)
CPPFLAGS += $(FLAGS)
LDLIBS += $$(pkg-config --libs libmisc)

all: station antenna

station: station.o visualisation.o config.o

station.o: station.c protocol.h common.h station.h

visualisation.o: visualisation.c visualisation.h station.h

config.o: config.cpp

antenna: antenna.o

antenna.o: antenna.c protocol.h common.h

clean:
	-rm -f station antenna station.o utils.o config.o antenna.o visualisation.o
