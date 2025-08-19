FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://10-eth.network.in"

LOCAL_IP = "${@bb.utils.contains("MACHINE_FEATURES", "antenna", "${ANTENNA_IP}", "${STATION_IP}", d)}"

NETDEV ?= "eth0"

do_install:append() {
    ETH_CFG=${WORKDIR}/10-${NETDEV}.network
    cp ${WORKDIR}/10-eth.network.in $ETH_CFG
    sed -i "s/##LOCAL_IP##/${LOCAL_IP}/g" $ETH_CFG
    sed -i "s/##NETDEV##/${NETDEV}/g" $ETH_CFG
    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 $ETH_CFG ${D}${sysconfdir}/systemd/network
}

FILES:${PN} += "${sysconfdir}/systemd/network/10-${NETDEV}.network"
