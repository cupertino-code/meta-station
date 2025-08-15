require control-common.inc

SRC_URI += " \
    file://stationctrl.in \
    file://station.c \
    file://visualisation.c \
    file://visualisation.h \
"

TARGET = "station"
