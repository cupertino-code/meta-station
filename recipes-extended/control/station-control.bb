require control-common.inc

SRC_URI += " \
    file://stationctrl.in \
    file://station.c \
    file://station.h \
    file://config.cpp \
    file://config.h \
    file://visualisation.c \
    file://visualisation.h \
    file://vrxtbl.yaml \
"
TARGET = "station"
RDEPENDS:${PN} += "liberation-fonts"

do_install:append() {
    install -d ${D}${sysconfdir}
    install -m 0644 vrxtbl.yaml ${D}${sysconfdir}
}

FILES:${PN} += "${sysconfdir}/vrxtbl.yaml"
