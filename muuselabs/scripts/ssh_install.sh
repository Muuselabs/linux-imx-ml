#/bin/bash
#
#ssh-keygen -f "/home/ppalmers/.ssh/known_hosts" -R 10.3.7.100
scp -r /tmp/to_jooki/ root@10.3.7.100:/tmp
ssh root@10.3.7.100 "sh - /tmp/to_jooki/target_install.sh $@"
