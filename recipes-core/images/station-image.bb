# Base this image on core-image-base
include recipes-core/images/core-image-base.bb

COMPATIBLE_MACHINE = "^rpi$"

KERNEL_EXTRA = " \
    kernel-modules \
"

EXTRA_TOOLS = " \
    bzip2 \
    curl \
    dosfstools \
    e2fsprogs-mke2fs \
    ethtool \
    fbset \
    findutils \
    grep \
    i2c-tools \
    ifupdown \
    iperf3 \
    iproute2 \
    iptables \
    less \
    lsof \
    netcat-openbsd \
    nmap \
    ntp ntp-tickadj \
    parted \
    procps \
    sysfsutils \
    tcpdump \
    util-linux \
    util-linux-blkid \
    unzip \
    wget \
    zip \
"

GSTREAMER = " \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gst-start \
"

IMAGE_INSTALL:append = " \
    ${GSTREAMER} \
    ${KERNEL_EXTRA} \
"
#IMAGE_INSTALL:append = " packagegroup-rpi-test"
