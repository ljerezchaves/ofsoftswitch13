#!/bin/bash

# Script to install on most recent versions of Ubuntu
# Tested on the LTS versions 14.04 and 16.04.
# Feel free to contribute to additional systems

UBUNTU_DEPS="g++ gcc cmake libc6-dev libpcap-dev libxerces-c3.1 libxerces-c-dev libpcre3 libpcre3-dev flex bison pkg-config autoconf libtool libboost-dev"

BUILD_DIR="$(pwd -P)"
install_deps()
{
    if [ $(lsb_release -si) = "Ubuntu" ]; then
        sudo apt-get install $UBUNTU_DEPS
    fi
}

switch()
{   
    if [ ${BUILD_DIR##*/} != "ofsoftswitch13" ]; then
        cd $BUILD_DIR/ofsoftswitch13
    else
        cd $BUILD_DIR
    fi
    ./boot.sh
    ./configure
    make
    sudo make install
}

install_deps
switch
