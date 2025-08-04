DESCRIPTION = "Commented config.txt file for the Raspberry Pi. \
               The Raspberry Pi config.txt file is read by the GPU before \
               the ARM core is initialised. It can be used to set various \
               system configuration parameters."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "^rpi$"

ENABLE_UART ??= "1"

WM8960="${@bb.utils.contains("MACHINE_FEATURES", "wm8960", "1", "0", d)}"

GPIO_SHUTDOWN_PIN ??= ""

inherit deploy nopackages

do_deploy:append() {
    CONFIG=${DEPLOYDIR}/${BOOTFILES_DIR_NAME}/config.txt

    echo "dtoverlay=disable-bt" >>$CONFIG
    echo "dtoverlay=adv7282m" >>$CONFIG
    echo "framebuffer_depth=16" >>$CONFIG
    echo "# Enable DRM VC4 V3D driver" >>$CONFIG
    echo "enable_tvout=1" >>$CONFIG
    echo "dtoverlay=vc4-fkms-v3d,composite" >>$CONFIG
    echo "disable_fw_kms_setup=1" >>$CONFIG
    if [ "${ENABLE_UART}" = "1" ]]; then
        echo "uart_2ndstage=1" >>$CONFIG
    fi
}
