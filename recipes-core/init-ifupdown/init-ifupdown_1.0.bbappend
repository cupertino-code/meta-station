FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

LOCAL_IP = "${@bb.utils.contains("MACHINE_FEATURES", "antenna", "${ANTENNA_IP}", "${STATION_IP}", d)}"

SRC_URI += "file://interfaces.in"

S = "${WORKDIR}"

do_configure:prepend() {
    INTERFACES=${WORKDIR}/interfaces
    cp ${WORKDIR}/interfaces.in $INTERFACES
    sed -i "s/##LOCAL_IP##/${LOCAL_IP}/g" $INTERFACES
}
