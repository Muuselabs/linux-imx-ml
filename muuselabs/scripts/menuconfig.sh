#! /bin/bash

set -eu

# Make paths relative to this script.
cd "$(dirname "$0")"

# Make paths relative to linux-2.6-imx
cd ../..

CONFIG_NAME="${1:-}"

#CONFIG_NAME=${CONFIG_OVERRIDE:-ml-imx6ulevk_defconfig}
#echo "Configuring: $CONFIG_NAME"

export ARCH=mips
export CROSS_COMPILE=mipsel-poky-linux-musl-
PATH=$PATH:$YOCTO_SDK_DIR/sysroots/x86_64-pokysdk-linux/usr/bin/mipsel-poky-linux-musl
make ${CONFIG_NAME} menuconfig
make savedefconfig
if [ ! "${CONFIG_NAME}" = "" ]; then
	cp -f defconfig ./arch/mips/configs/${CONFIG_NAME}
fi
