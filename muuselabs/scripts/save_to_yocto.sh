#! /bin/bash

# Manually apply changes from our Yocto recipe for Linux.

set -eu
cd "$(dirname "$0")"
cd ../.. # Linux repo root.

LINUX_RECIPE_FILES=${1:-$YOCTO_ROOT/sources/meta-jz-mips-ml/recipes-kernel/linux/linux-x1000/ml-j2000/}

cp ./arch/mips/boot/dts/ingenic/ml-j2000*.dts* $LINUX_RECIPE_FILES/
sort -n ./arch/mips/configs/ml-j2000_defconfig > $LINUX_RECIPE_FILES/defconfig

