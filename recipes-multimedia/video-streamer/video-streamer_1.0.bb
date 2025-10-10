SUMMARY = "Video streamer recipe"
DESCRIPTION = "Recipe created by bitbake-layers"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
LICENSE = "MIT"

inherit systemd cmake pkgconfig

DEPENDS += " \
    pkgconfig \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    opencv \
"

RDEPENDS:${PN} += "setpal"

SRC_URI += " \
    file://CMakeLists.txt \
    file://video-streamer.cpp \
    file://video-stream.in \
"

SERVICE_NAME = "video-stream"
SERVICE_FILE = "${SERVICE_NAME}.service"
#SYSTEMD_SERVICE:${PN} = "${SERVICE_FILE}"

S = "${WORKDIR}"
do_install() {
    cp ${S}/video-stream.in ${S}/${SERVICE_FILE}
    sed -i "s/##TARGET_PORT##/${VIDEO_STREAM_PORT}/g" ${S}/${SERVICE_FILE}
    sed -i "s/##TARGET_IP##/${STATION_IP}/g" ${S}/${SERVICE_FILE}
    install -d ${D}${bindir}
    install -m 0755 video-streamer ${D}${bindir}
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/${SERVICE_FILE} ${D}${systemd_system_unitdir}
}

FILES:${PN} += " \
    ${bindir}/video-streamer \
    ${systemd_system_unitdir}/${SERVICE_FILE} \
"
