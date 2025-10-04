This README file contains information on the contents of the meta-station layer.

Please see the corresponding sections below for details.

Patches
=======

Please submit any patches against the meta-station layer to the original github
repository https://github.com/cupertino-code/station-manifest.git

Maintainer: Code <code720220@gmail.com>

# I. Build steps

1. Fetch metadata:
  ```
  repo init -u https://github.com/cupertino-code/station-manifest.git -b scarthgap
  repo sync -c -j6
  ```
2. Prepare build system:
  ```
  TEMPLATECONF=$PWD/meta-station/conf/templates/station . poky/oe-init-build-env
  ```
3. Build:
  ```
  bitbake station-image
  ```
4. Flash to the SD card:
  ```
  cd tmp/deploy/images/raspberrypi0-2w
  sudo bmaptool copy station-image-raspberrypi0-2w.rootfs.wic.bz2 /dev/sda
  ```

# II. Build with docker
Build can be done in the docker container.
Build docker image:
```
docker build --build-arg "host_uid=$(id -u)" --build-arg "host_gid=$(id -g)" --tag "dr-yocto" /path/to/the/Dockerfile
```
Replace `/path/to/the/Dockerfile` with the path to Dockerfile. This file placed in the
`meta-station/scripts` folder. This step should be done once.
Run docker container:
```
docker run -it --rm -v $PWD:/public/Work dr-yocto
```
Directory where container was run will be mapped to the `/public/Work` folder in the container.
Perform steps 1, 2 and 3 from chapter "Build steps"
SD card image and other output files placed in the build/tmp/deploy/images/raspberrypi0-2w
directory:
* station-image-raspberrypi0-2w.rootfs.wic.bmap
* station-image-raspberrypi0-2w.rootfs.wic.bz2
* station-image-raspberrypi0-2w.rootfs.wic

# III. Windows notes
  To enable case sensitivity for a specific folder in NTFS, the fsutil.exe command can be used.
  This allows applications that require case-sensitive file access, such as those used in Linux
  environments via the Windows Subsystem for Linux (WSL), to function correctly.
  Steps to enable NTFS case sensitivity for a folder:
  Open Command Prompt as Administrator: Search for "Command Prompt" in the Start menu,
  right-click on the result, and select "Run as administrator."
  Execute the fsutil command: Type the following command and press Enter:
  ```
  fsutil.exe file SetCaseSensitiveInfo "C:\path\to\your\folder" enable
  ```
  Replace `C:\path\to\your\folder` with the actual full path to the directory where you want to
  enable case sensitivity.
  Once enabled for a directory, new subdirectories created within it will automatically inherit
  the case-sensitive attribute. This allows for the coexistence of files or folders with the same
  name but different casing (e.g., file.txt and File.txt) within that specific directory and its
  subdirectories.

# IV. Pinouts
  Station:
    GPIO0 - TX
    GPIO1 - RX
    GPIO17, GPIO18 - Encoder
    GPIO23 - Encoder button
    GPIO25 - Power switch

  Antenna
    GPIO14 - TX
    GPIO15 - RX
    GPIO25 - Power
    GPIO18 - Servo
