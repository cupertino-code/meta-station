RDEPENDS:${PN}-dev += " \
    opencv-staticdev \
"
SRC_INSTALL_PATH = "${prefix}/src/video-streamer"
S = "${WORKDIR}"
do_install() {
    install -d ${D}${SRC_INSTALL_PATH}
    install -m 0644 ${S}/video-streamer.cpp ${D}${SRC_INSTALL_PATH}
    install -m 0644 ${S}/CMakeLists.txt ${D}${SRC_INSTALL_PATH}
}

FILES:${PN}-dev += " \
    ${SRC_INSTALL_PATH}/* \
"
