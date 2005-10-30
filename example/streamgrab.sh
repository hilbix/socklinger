#!/bin/sh
# $Header$
#
# This is a grabber for any PROXY stream.
# Just call this with
# streamgrab.sh directory [commandline]
# to grab in and out stream into files.
#
# commandline is something like:
#	netcat proxy.localnet 8080
#
# $Log$
# Revision 1.1  2005-10-30 03:24:22  tino
# additional possibilities
#

DIR="$1"
shift

if [ ! -d "$DIR" ]
then
	echo "directory does not exist: $DIR"
	exit 1
fi
if [ -z "$*" ]
then
	echo "missing command!"
	exit 1
fi

ID="$DIR/streamgrab-`date +'%Y%m%d-%H%M%S'`-$$"

tee "$ID.in" |
"$@" |
tee "$ID.out"
