require basic-dev-image.bb

SPECIFIC_TOOLS:append = " \
    station-control-dev \
    libmisc-dev \
    crsf-station-dev \
    yaml-cpp-dev \
"

GSTREAMER:append = " \
"

do_showinstall() {
    bbwarn "IMAGE_INSTALL ${IMAGE_INSTALL}"
}

addtask do_showinstall
