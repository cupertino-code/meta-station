SUMMARY = "Custom USB mount/unmount rules"
LICENSE = "MIT"

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}-mount-rules:"

SRC_URI += " \
    file://usb-mount.sh \
    file://99-usb-mount.rules \
    file://usb-mount@.service \
"

do_install:append() {
    install -d ${D}${sysconfdir}/udev/rules.d/
    install -m 0644 ${WORKDIR}/99-usb-mount.rules ${D}${sysconfdir}/udev/rules.d/
    
    install -d ${D}/usr/local/bin/
    install -m 0755 ${WORKDIR}/usb-mount.sh ${D}/usr/local/bin/

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 usb-mount@.service ${D}${systemd_system_unitdir}
}

FILES:${PN} += " \
    ${sysconfdir}/udev/rules.d/99-usb-mount.rules \
    /usr/local/bin/usb-mount.sh \
    ${systemd_system_unitdir}/usb-mount@.service \
"
RDEPENDS:${PN} += "e2fsprogs dosfstools"
