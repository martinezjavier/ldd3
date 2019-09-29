#!/bin/bash

infile=infile
delay=1
outfile=outfile
strace=

function printusage
{
	echo "Usage: $0 [-i infile] [-d delay ] [-o outfile] [-s]"
	echo "  Writes outfile in non-blocking mode with the content of infile as it is updated"
	echo "  polling for changes to the file after delay seconds"
	echo "  When -s is specified, strace is used to trace the command"
	echo "  device default is ${device}"
	echo "  infile default is ${infile}"
	echo "  delay default is ${delay} seconds"
	echo "  See Linux Device Drivers 3rd Edition chapter 6 section \"Testing the Scullpipe Driver\""
	echo "  For example, to test non blocking IO on /dev/scullpipe for both read and write, you can:"
	echo "		Open one terminal window and enter command ./misc-progs/test/nonblock.sh -o /dev/scullpipe -s"
	echo "		Open a second terminal window and enter command ./misc-progs/test/nonblock.sh -i /dev/scullpipe -s"
	echo "		Open a 3rd terminal window and enter command tail -f  ./misc-progs/test/outfile"
	echo "		Open a 4th terminal window and ender command echo \"Hello World!\" >> ./misc-progs/test/infile"
	echo "		* You should see continuous polling at the rate specified by the delay argument on each"
	echo "			endpoint."
	echo "		* You should see the output moved from infile to outfile as each echo command is sent"
	echo "		* When building with DEBUG=y in the makefile, you should see debug messages in kern.log"
	echo "			indicating the utility nbtest read/wrote bytes successfully"
}

while getopts "i:o:d:sh" opt; do
	case ${opt} in
		i )
			infile=$OPTARG
			;;
		d )
			delay=$OPTARG
			;;
		o )
			outfile=$OPTARG
			;;
		s )
			strace=strace
			;;
		h )
			printusage
			exit 0
			;;

		\? )
			echo "Invalid option $OPTARG" 1>&2
			printusage
			exit 1
			;;
		: )
			echo "Invalid option $OPTARG requires an argument" 1>&2
			printusage
			exit 1
			;;
	esac
done

set -e
cd `dirname $0`
touch ${infile}
echo "Reading content of ${infile} through non blocking test to ${outfile}"
echo "${strace} ../nbtest ${delay} > ${outfile} < ${infile}"
${strace} ../nbtest ${delay} > ${outfile} < ${infile}
