## To build and upload a new kernel

Set user environment vars:
``` sh
export YOCTO_ROOT=$HOME/data2/jooki-dev
export ML_GIT=$HOME/data2/ml-git
export YOCTO_SDK_DIR=$HOME/yocto/sdk
```

Build and install:

Example flow for ml-j2000:
```bash
# note: setup the paths to match your env
cd $ML_GIT/linux-imx-ml

# this script copies the defconfig and dts files from the yocto build
# into the kernel tree
muuselabs/scripts/prep_from_yocto.sh 

# use menuconfig to change the config
# this will start menuconfig loading the ml-j2000_defconfig
# and after exiting ml-j2000_defconfig will be updated with changes
muuselabs/scripts/menuconfig.sh ml-j2000_defconfig

# do changes in source or whatever

# compile
muuselabs/scripts/compile.sh

# update out of tree modules (ml-j2000)
muuselabs/scripts/mod_compile.sh ${JOOKI_SOURCE_DIR}/esp32-code/esp-hosted/host/linux/host_driver/esp32/
muuselabs/scripts/mod_compile.sh ${YOCTO_ROOT}/sources/meta-ml-jooki/recipes-kernel/ml-j2000-htdrv/files/

# at this point the kernel and the modules are available in /tmp/to_jooki

# install /tmp/to_jooki to a target that is connected through usb networking
# NOTE: for ml-j2000 please avoid rebooting immediately because 
#       there are some out-of-tree modules that have to be updated first
muuselabs/scripts/ssh_install.sh
```
