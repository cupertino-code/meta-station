SUMMARY = "Antenna rotation control"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

SRC_URI = " \
    file://Makefile \
    file://utils.c \
    file://protocol.h \
    file://common.h \
"

SERVICE_NAME = "${TARGET}ctrl"
SERVICE_FILE = "${SERVICE_NAME}.service"
SYSTEMD_SERVICE:${PN} = "${SERVICE_FILE}"

DEPENDS += "cairo libgpiod"

EXTRA_OEMAKE = "${TARGET}"

S = "${WORKDIR}"

do_install() {
    cp ${SERVICE_NAME}.in ${SERVICE_FILE}
    sed -i "s/##STATION_IP##/${STATION_IP}/g" ${SERVICE_FILE}
    sed -i "s/##CONTROL_PORT##/${CONTROL_PORT}/g" ${SERVICE_FILE}

    install -d ${D}${bindir}
    install -m 0755 ${TARGET} ${D}${bindir}
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${SERVICE_FILE} ${D}${systemd_system_unitdir}
}

FILES:${PN} += "${bindir}/${TARGET}"
