#!/bin/bash

docker build --build-arg "host_uid=$(id -u)" --build-arg "host_gid=$(id -g)" --tag "dr-yocto" $1
