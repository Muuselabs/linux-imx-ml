#! /bin/bash

# This should be ran on the Jooki to update it.

set -eu

# Make paths relative.
cd "$(dirname "$0")"

# Install the modules in the root file-system.
echo "installing modules"
tar xzf lib.tgz -C /

echo "installing kernel"
cp uImage /boot/uImage

/sbin/depmod -a

sync

printf "Press any key to reboot..."
read dummy
/sbin/reboot
