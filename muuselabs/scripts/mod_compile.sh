#! /bin/bash

set -eu

SRC="$(readlink -f "$1")"

# Make paths relative to this script.
cd "$(dirname "$0")"

source options.rc

echo "Compiling module: $SRC"

# Make paths relative to linux root
cd ../..
KERNEL_SRC=$(pwd)

set -e
set +u
source $YOCTO_SDK_DIR/environment-setup-mips32r2el-poky-linux 2> /dev/null \
  || source $YOCTO_SDK_DIR/environment-setup-mips32r2el-poky-linux-musl
set -u

cd ${SRC}
make ARCH=$ARCH -C ${KERNEL_SRC} M=${SRC} modules
make ARCH=$ARCH -C ${KERNEL_SRC} M=${SRC} modules_install INSTALL_MOD_PATH=$TMP_FS
