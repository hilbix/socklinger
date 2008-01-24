#!/bin/bash
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
#	rm -rf PROXY/head PROXY/tmp
#	./socklinger -sn-2 :8081 example/proxy.sh [proxy proxyport [Proxyline]]&
#	SP="$!"
#	trap 'kill $SP' 0
#	tail --retry --follow=name PROXY/LOG PROXY/MIST
#
# Then tell Debian to use a proxy on port 8081 of the machine
# where socklinger runs to download Debian packages.
# Note that you must allow 2 parallel connects, as else
# the Debian Installer starves at one location (bug in d-i?).
#
# Note on PROXY, PROXYPORT and PROXYLINE:
#
# You can hand a proxy to this script.  The Proxyline is a line which is
# transparently sent to to Proxy.  You can use this for user authentication.
# This can be done with a line like:
# "Proxy-Authorization: Basic B64STRING"
# where B64STRING is the base64 encoded string of "user:password".
# To calculate this string you can try something like:
# php -r 'echo base64_encode("user:password");'; echo
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
# It does not check for changes on the Debian mirror!  To get new files,
# remove the header files (rm -rf PROXY/head).
# YOU HAVE TO DO THIS MANUALLY OR BY CRON!!!
#
# $Log$
# Revision 1.3  2008-01-24 23:42:38  tino
# Improved for proxy support etc.
#
# Revision 1.2  2004-09-20 01:52:14  tino
# Clarified, you must allow 2 proxies running in parallel.

cd PROXY || exit 1
umask 002

mkdir -p tmp

PROXY="$1"
PROXYPORT="$2"
PROXYLINE="$3"

#exec 2>>LOG

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

read -r -t 20 get url rest || mist "EOF" 
[ ".GET" != ".$get" ] && mist "no get"
case "$url" in
*)	url="`expr "$url" : '\(.*\).$'`";;
esac

case "$url" in
*..*)	mist ".. im URL";;
http://*)	;;
*)	mist "seltsames URL";;
esac

host="`expr "$url" : 'http://\([0-9a-z.]*\)[:/]'`"
port="`expr "$url" : 'http://[^:/]*:\([0-9]*\)/'`"

[ -z "$port" ] && port=80
[ -z "$host" ] && mist "host?"
case "$host" in
*.debian.org)	;;
### ADD ALLOWED DOMAINS HERE ###
*)		mist "no debian";;
esac

fhost="$host"
fport="$port"
[ -z "$PROXY" ] || fhost="$PROXY"
[ -z "$PROXYPORT" ] || fport="$PROXYPORT"

: fetch
fetch()
{
{
echo "$1 $url HTTP/1.0"
if [ 80 != "$port" ]
then
	echo "Host: $host:$port"
else
	echo "Host: $host"
fi
[ -z "$PROXYLINE" ] || echo "$PROXYLINE"
echo ""
} |
netcat -w60 "$fhost" "$fport"
}

spcparm()
{
eval $1='`expr "$line" : "[^:]*:[[:space:]]*\\([^[:space:]]*\\)"`'
}

fullparm()
{
eval $1='`expr "$line" : "[^:]*:[[:space:]]*\\(.*\\)\\$"`'
}

: parsehead
parsehead()
{
read response code rest || return 1
[ 200 = "$code" ] || return 1
while	line=""
	read -r line || return 1
	[ -n "$line" -a "" != "$line" ]
do
	case "$line" in
	[Cc]ontent-[Tt]ype:*)	spcparm $1type;;
	[Cc]ontent-[Ll]ength:*)	spcparm $1len;;
	[Ll]ast-[Mm]odified:*)	fullparm $1date;;
	esac
done
return 0
}

: cathead
cathead()
{
while	read -r line || exit 1
	[ -n "$line" -a "" != "$line" ]
do
	echo "$line"
done
echo "$line"
}

: chklen
chklen()
{
clen="`{ parsehead c; [ ":$ctype:$clen:$cdate" = ":$type:$len:$date" ] && wc -c; }`" || return 1
[ -z "$len" -o ".$len" = ".$clen" ]
}

durl="$url"
furl="$url"
case "$url" in
*/)	furl="$url""index.html";;
*)	durl="`dirname "$url"`";;
esac

if [ -f "data/$furl" ]
then
	if [ ! -s "head/$furl" ]
	then
		mkdir -p "head/$durl" || exit 1
		echo "++ $url" >> LOG
		fetch HEAD > "head/$furl"
	fi

	len=""
	if	{ ! [ "head/$furl" -nt "data/$furl" ]; } ||
		{ parsehead < "head/$furl" &&
		  chklen < "data/$furl" &&
		  touch -r "data/$furl" "head/$furl";
		}
	then
		echo "-- $url" >> LOG
		exec cat "data/$furl"
	fi

	echo "up $url" >> LOG
	mv -f --backup=t "head/$furl" "head/$furl.old"
fi

echo "do $url" >> LOG

TMP="tmp/$$"
fetch GET | tee "$TMP"

[ -f "$TMP" ] || exit 1
parsehead < "$TMP" || exit 1
chklen < "$TMP" || exit 1

echo "ok $url" >> LOG

mkdir -p "head/$durl" || exit 1
cathead < "$TMP" > "head/$furl"

mkdir -p "data/$durl" || exit 1
mv -f --backup=t "$TMP" "data/$furl"

