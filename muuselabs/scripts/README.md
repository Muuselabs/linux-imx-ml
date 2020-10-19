## To build and upload a new kernel

Set user environment vars:
``` sh
export YOCTO_ROOT=$HOME/data2/jooki-dev
export ML_GIT=$HOME/data2/ml-git
export YOCTO_SDK_DIR=$HOME/yocto/sdk
```

Build and install:
``` sh
cd $ML_GIT/linux-imx-ml
muuselabs/scripts/prep_from_yocto.sh
muuselabs/scripts/menuconfig.sh ml-j1000_defconfig
muuselabs/scripts/compile.sh ml-j1000.dtb
muuselabs/scripts/ssh_install.sh ml-j1000.db
```
