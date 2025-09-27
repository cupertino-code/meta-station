SUMMARY = "Bridge between uart and IP"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

SRC_URI = " \
    file://Makefile \
    file://crsf-bridge.c \
    file://crsf-bridge.in \
    file://circ_buf.c \
    file://circ_buf.h \
"

DEPENDS += "libmisc"
RDEPENDS:${PN} += "libmisc"

TARGET = "crsf-bridge"
SERVICE_NAME = "${TARGET}"
SERVICE_FILE = "${SERVICE_NAME}.service"
SYSTEMD_SERVICE:${PN} = "${SERVICE_FILE}"

CRSF_UART ?= "0"
UART = "\/dev\/ttyAMA${CRSF_UART}"

S = "${WORKDIR}"

do_install() {
    cp ${SERVICE_NAME}.in ${SERVICE_FILE}
    sed -i "s/##CONN_PARAMS##/${CONN_PARAMS}/g" ${SERVICE_FILE}
    sed -i "s/##TARGET_IP##/${TARGET_IP}/g" ${SERVICE_FILE}
    sed -i "s/##UART##/${UART}/g" ${SERVICE_FILE}

    install -d ${D}${bindir}
    install -d ${D}/root
    install -m 0755 ${TARGET} ${D}${bindir}
    install -m 0755 ${TARGET} ${D}/root
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${SERVICE_FILE} ${D}${systemd_system_unitdir}
}

FILES:${PN} += "${bindir}/${TARGET} ${systemd_system_unitdir}/${SERVICE_FILE} /root/${TARGET}"
