SUMMARY = "bitbake-layers recipe"
DESCRIPTION = "Recipe created by bitbake-layers"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
LICENSE = "MIT"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

RDEPENDS:${PN} = "bash"

SRC_URI += " \
    file://stream_server.sh \
    file://camera-stream.service \
"

SYSTEMD_SERVICE:${PN} = "camera-stream.service"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/stream_server.sh ${D}${bindir}/stream_server
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/camera-stream.service ${D}${systemd_system_unitdir}
}

FILES:${PN} += " \
    ${bindir}/stream_server \
    ${systemd_system_unitdir}/camera-stream.service \
"
