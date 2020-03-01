#!/bin/sh
module=$1
# Use the same name for the device as the name used for the module
device=$1
# Support read/write for owner and group, read only for everyone using 644
mode="664"

if [ $# -ne 1 ]; then
	echo "Wrong number of arguments"
	echo "usage: $0 module_name"
	echo "Will create a corresponding device /dev/module_name associated with module_name.ko"
	exit 1
fi

set -e
# Group: since distributions do it differently, look for wheel or use staff
# These are groups which correspond to system administrator accounts
if grep -q '^staff:' /etc/group; then
    group="staff"
else
    group="wheel"
fi

echo "Load our module, exit on failure"
insmod ./$module.ko $* || exit 1
echo "Get the major number (allocated with allocate_chrdev_region) from /proc/devices"
major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
if [ ! -z ${major} ]; then
    echo "Remove any existing /dev node for /dev/${device}"
    rm -f /dev/${device}
    echo "Add a node for our device at /dev/${device} using mknod"
    mknod /dev/${device} c $major 0
    echo "Change group owner to ${group}"
    chgrp $group /dev/${device}
    echo "Change access mode to ${mode}"
    chmod $mode  /dev/${device}
else
    echo "No device found in /proc/devices for driver ${module} (this driver may not allocate a device)"
fi
