SUMMARY = "RTP Video stream client"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

SRC_URI = " \
    file://video-out.sh \
    file://video-out.in \
"

SERVICE_NAME = "video-out"
SERVICE_FILE = "${SERVICE_NAME}.service"
SYSTEMD_SERVICE:${PN} = "${SERVICE_FILE}"

S = "${WORKDIR}"

do_install() {
    cp ${SERVICE_NAME}.in ${SERVICE_FILE}
    sed -i "s/##RTP_PORT##/${VIDEO_STREAM_PORT}/g" ${SERVICE_FILE}

    install -d ${D}${bindir}
    install -m 0755 video-out.sh ${D}${bindir}/video-out
    install -d ${D}/root
    install -m 0755 video-out.sh ${D}/root
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${SERVICE_FILE} ${D}${systemd_system_unitdir}
}

FILES:${PN} += " \
    ${bindir}/video-out \
    /root/video-out.sh \
    ${systemd_system_unitdir}/${SERVICE_FILE} \
"
