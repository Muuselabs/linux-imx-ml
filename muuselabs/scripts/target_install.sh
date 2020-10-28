#! /bin/bash

# This should be ran on the Jooki to update it.

set -eu

# Make paths relative.
cd "$(dirname "$0")"

# Install the modules in the root file-system.
tar xzf lib.tgz -C /

dd if=/tmp/to_jooki/uImage of=/dev/mmcblk0p1 bs=1024

sync

printf "Press any key to reboot..."
read dummy
/sbin/reboot
