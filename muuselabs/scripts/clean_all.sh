#! /bin/bash

set -eu

# Make paths relative to linux-2.6-imx
cd "$(dirname "$0")"
cd ../..

set +eu
CONFIG_NAME=$1
source ../sdk/environment-setup-*-linux-gnueabi
set -eu

make mrproper
if [ ! "${CONFIG_NAME}" = "" ]; then
	echo "Setting up for: $CONFIG_NAME"
	make ${CONFIG_NAME}
fi

