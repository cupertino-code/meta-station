SUMMARY = "Set PAL standard for v4l2"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

SRC_URI = " \
    file://setpal.service \
"

SYSTEMD_SERVICE:${PN} = "setpal.service"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 setpal.service ${D}${systemd_system_unitdir}
}
