#!/bin/sh
#
# $Header$
#
# WARNING!  THIS SCRIPT WAS DONE IN A HURRY!
#
# This is an extremely simple proxy for network based DEBIAN install.
# It can be started on demand and caches all files downloaded, so they
# need not be downloaded again.
# It can run on another Linux machine or under Windows running Cygwin.
# It is able to restart downloads from where they broke.
#
# Usage:
#	make
#	mkdir -p PROXY
#	rm -rf PROXY/head
#	./socklinger 2@:8081 example/proxy.sh &
#	tail --follow=file PROXY/LOG PROXY/MIST
#
# Then tell Debian to use a proxy on port 8081 of the machine
# where socklinger runs to download Debian packages.
# Note that you must allow 2 parallel connects, as else
# the Debian Installer starves at one location (bug in d-i?).
#
# You need a directory named PROXY where the data is written to.
# In PROXY/LOG a log is written.
# In PROXY/MIST the errors are written ("Mist" in German means Bullshit).
#
# THIS SCRIPT HAS A LOT OF BUGS:
#
# It has a lot of race conditions.  So never allow more than one Debian
# to download new packages via this script in parallel!
#
# It is only working with Debian install and apt-get.
# You can only access mirrors in the domain debian.org.
# See ### ADD ALLOWED DOMAINS HERE ### to enter more alloed domains.
#
# If you don't have a fast internet connectivity, downloads take too long.
# The wget on the Debian install then times out and you will get errors.
# (This is true for apt-get, too!  This is a bug of this script, not Debian!)
# HOWEVER THE DOWNLOAD STILL IS RUNNING IN THE BACKGROUND, SO JUST WAIT
# UNTIL THE PROXY SCRIPT TELLS YOU IT'S FINISHED.
# When the download finished, you have to restart the install process!
# This is a bit clumsy, perhaps, but after a few tries you will have
# downloaded everything you need.  I like it as it is, as usually the
# machine to install has no internet connectivity, and usually I repeat
# the install several times until I am happy with it.
#
# It does not check for changes on the Debian mirror!  To get new files,
# remove the header files (rm -rf PROXY/head).
# YOU HAVE TO DO THIS MANUALLY OR BY CRON!!!
#
# $Log$
# Revision 1.2  2004-09-20 01:52:14  tino
# Clarified, you must allow 2 proxies running in parallel.
#
# Revision 1.1  2004/08/15 22:19:37  Administrator
# added

cd "`dirname "$0"`/PROXY" || exit 1
umask 002

exec 2>>LOG

mist()
{
{
echo "==============================="
echo "`date` MIST: $*"
echo "host=$host"
echo "port=$port"
echo "$get $url $rest"
while read -t10 line
do
	echo "$line"
done
} >> MIST
exit 1
}

if [ 0 != "$#" ]
then
	url="$*"
else
	read -r -t 20 get url rest || mist "EOF" 
	[ ".GET" != ".$get" ] && mist "no get"
	case "$url" in
	*)	url="`expr "$url" : '\(.*\).$'`";;
	esac
fi

case "$url" in
*..*)	mist ".. im URL";;
http://*)	;;
*)	mist "seltsames URL";;
esac

host="`expr "$url" : 'http://\([0-9a-z.]*\)[:/]'`"
port="`expr "$url" : 'http://[^:]*:\([0-9]*\)/'`"

[ -z "$port" ] && port=80
[ -z "$host" ] && mist "host?"
case "$host" in
*.debian.org)	;;
### ADD ALLOWED DOMAINS HERE ###
*)		mist "no debian";;
esac

if [ ! -s "head/$url" ]
then
	mkdir -p "head/`dirname "$url"`" || exit 1
	echo "++ $url" >> LOG
	{
	echo "HEAD $url HTTP/1.0"
	if [ 80 != "$port" ]
	then
		echo "Host: $host:$port"
	else
		echo "Host: $host"
	fi
	echo ""
	} |
	netcat -w60 "$host" "$port" > "head/$url"
fi

cat "head/$url"

len=""
{
read response code rest || exit 1
[ 200 = "$code" ] || exit 1
while	read -r line || exit 1
	[ -n "$line" -a "" != "$line" ]
do
	case "$line" in
	[Cc]ontent-[Tt]ype:*)	type="`expr "$line" : '[^:]*:[[:space:]]*\([^[:space:]]*\)'`";;
	[Cc]ontent-[Ll]ength:*)	len="`expr "$line" : '[^:]*:[[:space:]]*\([^[:space:]]*\)'`";;
	esac
done
} < "head/$url"

if [ -f "data/$url" ]
then
	echo "-- $url" >> LOG
else
	mkdir -p "tmp/`dirname "$url"`" || exit 1
	echo "do $url ($len)" >> LOG
	curl -C- -o"tmp/$url" "$url" >>LOG
	[ -f "tmp/$url" ] || exit 1
	[ "$len" = "`find "tmp/$url" -printf "%s"`" ] || exit 1
	echo "ok $url" >> LOG
	mkdir -p "data/`dirname "$url"`" || exit 1
	mv -f "tmp/$url" "data/$url" || exit 1
fi

exec cat "data/$url"
