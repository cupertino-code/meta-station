include recipes-core/images/core-image-base.bb

inherit extrausers

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
    openssh-client \
"

GSTREAMER = " \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    v4l-utils \
"

DEV_TOOLS = " \
    minicom \
    socat \
    raspi-gpio \
"

# Додаємо підтримку SSH-сервера (OpenSSH)
IMAGE_FEATURES += "ssh-server-dropbear"

# Password for root user
#EXTRA_USERS_PARAMS = "usermod -P root root;"

IMAGE_INSTALL:append = " \
    ${GSTREAMER} \
    ${KERNEL_EXTRA} \
    ${DEV_TOOLS} \
    ${SPECIFIC_TOOLS} \
"

PYTHON = " \
    python3 \
    python3-pygobject \
"

IMAGE_INSTALL:append = " \
    ${PYTHON} \
"

#IMAGE_INSTALL:append = " packagegroup-rpi-test"
