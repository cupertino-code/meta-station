SUMMARY = "Custom USB mount/unmount rules"
LICENSE = "MIT"

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}-mount-rules:"

SRC_URI += " \
    file://mount_new.sh \
"
MOUNT_BASE = "/media"
do_install:prepend:() {
    cp ${WORKDIR}/mount_new.sh ${WORKDIR}/mount.sh
}

RDEPENDS:${PN} += "e2fsprogs dosfstools exfat-utils fuse-exfat libgpiod-tools"
