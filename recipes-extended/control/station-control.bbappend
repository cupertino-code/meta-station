SRC_INSTALL_PATH = "${prefix}/src/control"

SRCS = "Makefile common.h config.cpp config.h protocol.h station.c station.h visualisation.c visualisation.h"

do_install:append() {
    install -d ${D}${SRC_INSTALL_PATH}
    for fil in ${SRCS}; do
        install -m 0644 $fil ${D}${SRC_INSTALL_PATH}
    done
}

FILES:${PN}-dev += "${SRC_INSTALL_PATH}/*"
