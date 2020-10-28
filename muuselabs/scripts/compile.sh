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
DTB="${1:-ingenic/ml-j2000.dtb}"
echo "Compiling with DTB: $DTB"

# Make paths relative to linux-2.6-imx
cd ../..

set +eu
source $YOCTO_SDK_DIR/environment-setup-mips32r2el-poky-linux
set -eu

IMAGE_TGT=uImage

make -j4 all ${IMAGE_TGT} ${DTB}
make -j4 modules || true
make -j4 headers || true

cp ./arch/$ARCH/boot/dts/${DTB} $TMP_FS/
cp ./arch/$ARCH/boot/${IMAGE_TGT} $TMP_FS/

make ARCH=$ARCH modules_install INSTALL_MOD_PATH=$TMP_FS
( cd $TMP_FS && tar czf lib.tgz lib/ && rm -r lib/ )

printf "\nCopy these files to Jooki and run $INSTALLER\n"

du -hs $TMP_FS/*
