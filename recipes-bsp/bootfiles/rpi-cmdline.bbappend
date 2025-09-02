CRSF_UART ?= "0"
CMDLINE_SERIAL = "${@oe.utils.conditional("CRSF_UART", "0", "console=tty1", "console=serial0,115200", d)}"

DISPLAY_WIDTH ?= "1024"
DISPLAY_HEIGHT ?= "576"
CMDLINE_STATION = "${@bb.utils.contains("MACHINE_FEATURES", "station", \
        "video=HDMI-A-1:${DISPLAY_WIDTH}x${DISPLAY_HEIGHT}M@60 vt.global_cursor_default=0", "", d)}"

CMDLINE += " \
    ${CMDLINE_STATION} \
"
