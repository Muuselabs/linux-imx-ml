#! /bin/bash

# This should be ran on the Jooki to update it.

set -eu

# Make paths relative.
cd "$(dirname "$0")"

# allow to override the DTB through the command line
set +eu
DTB_OVERRIDE=$1
set -eu
DTB=${DTB_OVERRIDE:-ml-j1000.dtb}
echo "Installing with DTB: $DTB"

# Install the modules in the root file-system.
tar xzf lib.tgz -C /

flash_erase  /dev/mtd2 0 0
nandwrite -p /dev/mtd2             ./zImage
nandwrite -p /dev/mtd2 -s 0x7e0000 ./${DTB}
sync

printf "Press any key to reboot..."
read dummy
shutdown -r now
