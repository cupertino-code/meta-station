SUMMARY = "Video streamer recipe"
DESCRIPTION = "Recipe created by bitbake-layers"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
LICENSE = "MIT"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

DEPENDS += "python3 python3-pygobject"

RDEPENDS:${PN} += "python3"

SRC_URI += " \
    file://camera-stream.py \
    file://camera-stream.in \
"

SERVICE_NAME = "camera-stream"
SERVICE_FILE = "${SERVICE_NAME}.service"
SYSTEMD_SERVICE:${PN} = "${SERVICE_FILE}"

S = "${WORKDIR}"
do_install() {
    cp camera-stream.in ${SERVICE_FILE}
    sed -i "s/##TARGET_PORT##/${VIDEO_STREAM_PORT}/g" ${SERVICE_FILE}
    sed -i "s/##TARGET_IP##/${STATION_IP}/g" ${SERVICE_FILE}
    install -d ${D}${bindir}
    install -m 0755 camera-stream.py ${D}${bindir}/camera-stream
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 camera-stream.service ${D}${systemd_system_unitdir}
}

FILES:${PN} += " \
    ${bindir}/camera-stream \
    ${systemd_system_unitdir}/${SERVICE_FILE} \
"
