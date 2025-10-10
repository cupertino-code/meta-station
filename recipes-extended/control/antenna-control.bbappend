SRC_INSTALL_PATH = "${prefix}/src/control"

SRCS = "Makefile common.h  protocol.h  antenna.c"

do_install:append() {
    install -d ${D}${SRC_INSTALL_PATH}
    for fil in ${SRCS}; do
        install -m 0644 $fil ${D}${SRC_INSTALL_PATH}
    done
}

FILES:${PN}-dev += "${SRC_INSTALL_PATH}/*"
