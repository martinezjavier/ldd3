#!/bin/sh
module=$1
device=$1

if [ $# -ne 1 ]; then
	echo "Wrong number of arguments"
	echo "usage: $0 module_name"
	echo "Will unload the module specified by module_name and remove assocaited device"
	exit 1
fi

# invoke rmmod with all arguments we got
rmmod $module || exit 1

# Remove stale nodes

rm -f /dev/${device}
