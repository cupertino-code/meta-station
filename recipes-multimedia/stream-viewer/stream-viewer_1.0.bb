SUMMARY = "RTP stream viewer with recording"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

SRC_URI = " \
    file://stream-view.py \
    file://stream-view.in \
"

RDEPENDS_${PN} += "rpi-gpio"
DEPENDS += "python3-pygobject rpi-gpio"

SERVICE_NAME = "stream-view"
SERVICE_FILE = "${SERVICE_NAME}.service"
SYSTEMD_SERVICE:${PN} = "${SERVICE_FILE}"

S = "${WORKDIR}"

do_install() {
    cp ${SERVICE_NAME}.in ${SERVICE_FILE}
    sed -i "s/##RTP_PORT##/${VIDEO_STREAM_PORT}/g" ${SERVICE_FILE}
    install -d ${D}/${bindir}
    install -m 0755 stream-view.py ${D}/${bindir}/stream-view
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${SERVICE_FILE} ${D}${systemd_system_unitdir}
}

FILES:${PN} += "${bindir}/stream-view"
