#/bin/bash
#

set -eu

# Make paths relative to this script.
cd "$(dirname "$0")"

source options.rc

[ -d $TMP_FS/lib ] && ( cd $TMP_FS && tar czf lib.tgz lib/ && rm -r lib/ )

cp -f $INSTALLER $TMP_FS

printf "\nInstalling to jooki\n"

du -hs $TMP_FS/*

scp -r /tmp/to_jooki/ root@10.3.7.100:/home/root/
ssh root@10.3.7.100 "sh - /home/root/to_jooki/target_install.sh $@"
