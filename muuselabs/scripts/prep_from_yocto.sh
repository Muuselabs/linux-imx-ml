#! /bin/bash

# Manually apply changes from our Yocto recipe for Linux.

set -eu
cd "$(dirname "$0")"
cd ../.. # Linux repo root.

LINUX_RECIPE_FILES=$YOCTO_ROOT/sources/meta-freescale-ml/recipes-kernel/linux/linux-muuselabs-4.14.98/ml-j1000

cp $LINUX_RECIPE_FILES/*.dts     ./arch/arm/boot/dts
cp $LINUX_RECIPE_FILES/*.dtsi    ./arch/arm/boot/dts
cp $LINUX_RECIPE_FILES/defconfig ./arch/arm/configs/ml-j1000_defconfig
