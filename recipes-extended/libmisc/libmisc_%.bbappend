SRC_INSTALL_PATH = "${prefix}/src/libmisc"

SRCS = "Makefile  crsf_protocol.h  shmem.c  shmem.h  utils.c  utils.h"

do_install:append() {
    install -d ${D}${SRC_INSTALL_PATH}
    for fil in ${SRCS}; do
        install -m 0644 $fil ${D}${SRC_INSTALL_PATH}
    done
}

FILES:${PN}-dev += "${SRC_INSTALL_PATH}/*"
#INSANE_SKIP:${PN}-dev += "installed-vs-shipped"
