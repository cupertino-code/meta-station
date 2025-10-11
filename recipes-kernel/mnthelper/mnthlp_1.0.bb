SUMMARY = "Linux Kernel Module with a SysFS interface"
DESCRIPTION = "A simple kernel module demonstrating SysFS interaction."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://LICENSE.GPL-2.0-only;md5=4ee23c52855c222cba72583d301d2338"

# Версія
PV = "1.0"
PR = "r0"

SRC_URI = " \
    file://LICENSE.GPL-2.0-only \
    file://mnthlp.c \
    file://Makefile \
"

inherit module

S = "${WORKDIR}"

FILES:${PN} += "/lib/modules/${KERNEL_VERSION}/extra/mnthlp.ko"
