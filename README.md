This README file contains information on the contents of the meta-station layer.

Please see the corresponding sections below for details.

Patches
=======

Please submit any patches against the meta-station layer to the original github
repository https://github.com/cupertino-code/station-manifest.git

Maintainer: Code <code720220@gmail.com>

# I. Build steps

Install dependencies:
  ```
  sudo apt-get install build-essential chrpath cpio debianutils diffstat file gawk gcc git iputils-ping libacl1 liblz4-tool locales python3 python3-git python3-jinja2 python3-pexpect python3-pip python3-subunit socat texinfo unzip wget xz-utils zstd
  ```
Install moulin. Read documentation at [moulin](https://moulin.readthedocs.io/en/latest)

1. Fetch metadata:
  ```
  git clone https://github.com/cupertino-code/meta-station.git -b v1.0
  ```
2. Prepare build system:

  Copy meta-station/scriprs/station.yaml to empty directory and run command:
  ```
  moulin station.yaml
  ```
3. Build:
  ```
  ninja 
  ```
4. Flash to the SD card:
  * Antenna image:
  ```
  cd yocto/build-antenna/tmp/deploy/images/raspberrypi0-2w
  sudo bmaptool copy antenna-image-raspberrypi0-2w.rootfs.wic.bz2 /dev/sda
  ```
  * Station image:
  ```
  cd yocto/build-station/tmp/deploy/images/raspberrypi4
  sudo bmaptool copy station-image-raspberrypi4.rootfs.wic.bz2 /dev/sda
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
SD card image and other output files placed in the yocto/build-antenna/build/tmp/deploy/images/raspberrypi0-2w
directory for antenna image:
* antenna-image-raspberrypi0-2w.rootfs.wic.bmap
* antenna-image-raspberrypi0-2w.rootfs.wic.bz2
* antenna-image-raspberrypi0-2w.rootfs.wic

  and in the yocto/build-station/build/tmp/deploy/images/raspberrypi4 for station image:
* station-image-raspberrypi4.rootfs.wic.bmap
* station-image-raspberrypi4.rootfs.wic.bz2
* station-image-raspberrypi4.rootfs.wic
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
  *  GPIO0 - TX
  *  GPIO1 - RX
  *  GPIO17, GPIO18 - Encoder
  *  GPIO23 - Encoder button
  *  GPIO25 - Power switch
  *  GPIO22 - Writing led
  *  GPIO24 - Write switch

  Antenna
  *  GPIO14 - TX
  *  GPIO15 - RX
  *  GPIO25 - Power
  *  GPIO18 - Servo
  *  GPIO16 - Low Power
