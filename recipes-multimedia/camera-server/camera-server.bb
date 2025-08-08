SUMMARY = "bitbake-layers recipe"
DESCRIPTION = "Recipe created by bitbake-layers"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
LICENSE = "MIT"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd

RDEPENDS:${PN} = "bash"

SRC_URI += " \
    file://stream_server.sh \
    file://camera-stream.in \
"

SERVICE_NAME = "camera-stream.service"
SYSTEMD_SERVICE:${PN} = "${SERVICE_NAME}"

TARGET_IP = "192.168.13.20"
TARGET_PORT = "5000"

do_install() {
    SERVICE=${WORKDIR}/${SERVICE_NAME}
    cp ${WORKDIR}/camera-stream.in $SERVICE
    sed -i "s/##TARGET_PORT##/${TARGET_PORT}/g" $SERVICE
    sed -i "s/##TARGET_IP##/${TARGET_IP}/g" $SERVICE
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/stream_server.sh ${D}${bindir}/stream_server
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/camera-stream.service ${D}${systemd_system_unitdir}
}

FILES:${PN} += " \
    ${bindir}/stream_server \
    ${systemd_system_unitdir}/${SERVICE_NAME} \
"
