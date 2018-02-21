#! /bin/bash

set -eu

# Make paths relative to this script.
cd "$(dirname "$0")"

# Make paths relative to linux-2.6-imx
cd ../..

SDK_DIR=`cd "../sdk"; pwd`

CONFIG_NAME=""
set +eu
CONFIG_NAME=$1
set -eu
#CONFIG_NAME=${CONFIG_OVERRIDE:-ml-imx6ulevk_defconfig}
#echo "Configuring: $CONFIG_NAME"

export ARCH=arm
export CROSS_COMPILE=arm-fslc-linux-gnueabi-
PATH=$PATH:$SDK_DIR/sysroots/x86_64-fslcsdk-linux/usr/bin/arm-fslc-linux-gnueabi
make ${CONFIG_NAME} menuconfig
make savedefconfig
if [ ! "${CONFIG_NAME}" = "" ]; then
	cp -f defconfig ./arch/arm/configs/${CONFIG_NAME}
fi
