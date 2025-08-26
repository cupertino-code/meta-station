DESCRIPTION = "Commented config.txt file for the Raspberry Pi. \
               The Raspberry Pi config.txt file is read by the GPU before \
               the ARM core is initialised. It can be used to set various \
               system configuration parameters."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

ANTENNA = "${@bb.utils.contains("MACHINE_FEATURES", "antenna", "1", "", d)}"
STATION = "${@bb.utils.contains("MACHINE_FEATURES", "station", "1", "", d)}"

CRSF_UART ?= "0"

COMPATIBLE_MACHINE = "^rpi$"

GPIO_SHUTDOWN_PIN ??= ""

inherit deploy nopackages

do_deploy:append() {
    CONFIG=${DEPLOYDIR}/${BOOTFILES_DIR_NAME}/config.txt

    echo "dtoverlay=disable-bt" >> $CONFIG
    echo "framebuffer_depth=16" >> $CONFIG
    echo "# Enable DRM VC4 V3D driver" >> $CONFIG
    echo "enable_tvout=1" >>$CONFIG
    echo "disable_fw_kms_setup=1" >> $CONFIG
    if [ x${ANTENNA} = "x1" ]; then
        echo "dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4" >> $CONFIG
        echo "dtoverlay=adv7282m" >> $CONFIG
    fi
    if [ x${CRSF_UART} != "x0" ]; then
        echo "dtoverlay=uart${CRSF_UART}" >> $CONFIG
        if [ x${STATION} = "x1" ]; then
            echo "uart_2ndstage=1" >> $CONFIG
        fi
    fi
    if [ x${HDMI_FORCE} = "x1" ]; then
        echo "hdmi_force_hotplug=1" >> $CONFIG
        echo "hdmi_group=1" >> $CONFIG
        echo "hdmi_mode=16" >> $CONFIG
    fi
    if [ x${STATION} = "x1" ]; then
        echo "display_auto_detect=1" >> $CONFIG
    fi
}
