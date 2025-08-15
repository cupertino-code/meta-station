FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "\
    file://motd.antenna \
    file://motd.station \
"
TARGET = "${@bb.utils.contains("MACHINE_FEATURES", "antenna", "antenna", "station", d)}"
S = "${WORKDIR}"

do_configure:append() {
    cp motd.${TARGET} motd
}
