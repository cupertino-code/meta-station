SUMMARY = "RTP stream viewer with recording"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

TARGET = "stream-view.py"

RDEPENDS_${PN} += "rpi-gpio"
DEPENDS += "python3-pygobject rpi-gpio"

SRC_URI = " \
    file://${TARGET} \
"

S = "${WORKDIR}"

do_install() {
    install -d ${D}/root
    install -m 0755 ${TARGET} ${D}/root
}

FILES:${PN} += "/root/${TARGET}"
