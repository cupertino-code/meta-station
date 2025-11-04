SUMMARY = "UDEV mount rules"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI += " \
    file://99-local.rules \
    file://usb-mount.sh \
    file://usb-mount@.service \
    file://updater.sh \
"
MOUNT_BASE = "/media"
S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/99-local.rules     ${D}${sysconfdir}/udev/rules.d
    sed -i 's|@MOUNT_BASE@|${MOUNT_BASE}|g' ${WORKDIR}/usb-mount.sh
    install -d ${D}${sysconfdir}/udev/scripts
    install -m 0755 ${WORKDIR}/usb-mount.sh ${D}${sysconfdir}/udev/scripts
    install -m 0755 ${WORKDIR}/updater.sh ${D}${sysconfdir}/udev/scripts
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/usb-mount@.service ${D}${systemd_system_unitdir}
}

RDEPENDS:${PN} += "e2fsprogs dosfstools exfat-utils fuse-exfat libgpiod-tools"

FILES:${PN} = " \
    ${systemd_system_unitdir}/usb-mount@.service \
    ${sysconfdir}/udev/rules.d/99-local.rules \
    ${sysconfdir}/udev/scripts/usb-mount.sh \
    ${sysconfdir}/udev/scripts/updater.sh \
"
