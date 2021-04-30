#! /bin/bash

set -eu

# Make paths relative to this script.
cd "$(dirname "$0")"

source options.rc

rm -rf $TMP_FS || true
mkdir $TMP_FS

cp $INSTALLER $TMP_FS

# allow to override the DTB through the command line
DTB="${1:-ingenic/ml-j2000.dtb}"
echo "Compiling with DTB: $DTB"

# Make paths relative to linux-2.6-imx
cd ../..

set -e
set +u
source $YOCTO_SDK_DIR/environment-setup-mips32r2el-poky-linux 2> /dev/null \
  || source $YOCTO_SDK_DIR/environment-setup-mips32r2el-poky-linux-musl
set -u

make -j4 all ${IMAGE_TGT} ${DTB}
make -j4 modules || true
make -j4 headers || true

cp ./arch/$ARCH/boot/dts/${DTB} $TMP_FS/
cp ./arch/$ARCH/boot/${IMAGE_TGT} $TMP_FS/

make ARCH=$ARCH modules_install INSTALL_MOD_PATH=$TMP_FS

