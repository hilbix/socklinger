#!/bin/sh
#
# $Header$
#
# Just a harmless testing script
#
# $Log$
# Revision 1.2  2005-10-30 03:24:22  tino
# additional possibilities
#
# Revision 1.1  2004/08/16 01:30:01  Administrator
# initial add

echo "$$: This is a harmless testing script!"
echo "$$: SOCKLINGER_NR=$SOCKLINGER_NR SOCKLINGER_PEER=$SOCKLINGER_PEER"
echo -n "$$: "
for a in 9 8 7 6 5 4 3 2 1 0
do
	echo -n "$a "
	sleep 1
done
echo "bye bye"
