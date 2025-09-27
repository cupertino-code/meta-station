SUMMARY = "Library with utils for ground station and antenna"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://Makefile \
    file://shmem.c \
    file://shmem.h \
    file://utils.c \
    file://utils.h \
    file://crsf_protocol.h \
    file://libmisc.pc \
"

inherit pkgconfig

S = "${WORKDIR}"

SOLIBS = ".so" 
FILES_SOLIBSDEV = ""

do_install:append() {
    oe_runmake install INSTALL_DIR="${D}"
    install -d ${D}${libdir}/pkgconfig
    install -m 644 ${WORKDIR}/libmisc.pc ${D}${libdir}/pkgconfig/
}

FILES:${PN}-dev += "${includedir}/libmisc ${libdir}/pkgconfig/misc.pc ${libdir}/*.a"
