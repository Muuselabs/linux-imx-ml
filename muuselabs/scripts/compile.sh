#! /bin/bash

set -eu

# Make paths relative to this script.
cd "$(dirname "$0")"

INSTALLER='target_install.sh'

TMP_FS=/tmp/to_jooki
rm -rf $TMP_FS || true
mkdir $TMP_FS

cp $INSTALLER $TMP_FS

# allow to override the DTB through the command line
set +eu
DTB_OVERRIDE=$1
set -eu
DTB=${DTB_OVERRIDE:-ml-j1000.dtb}
echo "Compiling with DTB: $DTB"

# Make paths relative to linux-2.6-imx
cd ../..

set +eu
source ../sdk/environment-setup-*-linux-gnueabi
set -eu

make -j4 zImage modules ${DTB}

cp ./arch/arm/boot/dts/${DTB} $TMP_FS/
cp ./arch/arm/boot/zImage $TMP_FS/

make ARCH=arm modules_install INSTALL_MOD_PATH=$TMP_FS
( cd $TMP_FS && tar czf lib.tgz lib/ && rm -r lib/ )

printf "\nCopy these files to Jooki and run $INSTALLER\n"

du -hs $TMP_FS/*
