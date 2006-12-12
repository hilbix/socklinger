#!/bin/sh
# $Header$
#
# Portcullis Script for socklinger.
# Use this to protect a service.
#
# Example:
#	( cd ..; make install; )
#	/usr/local/bin/socklinger 10@:23 /usr/local/sbin/portcullis.sh '[ .127.0.0.1 = ".$IP" ] || echo Password' /usr/sbin/sshd -i
#
# $Log$
# Revision 1.1  2006-12-12 10:36:42  tino
# See ChangeLog
#

TMP=/tmp
ALLOW=3600
BLOCK=60
if [ 0 = $# ]
then
	echo "Usage: `basename "$0"` 'checkscript args' command args.." >&2
	echo "	If permission is still valid (timestamp), peer is permitted." >&2
	echo "	Else checkscript is invoked." >&2
	echo "	If checkscript returns FALSE access is denied." >&2
	echo "	If checkscript returns a text, a passord prompt is done." >&2
	echo "	The password must be entered successfully to become permitted." >&2
	echo "	If checkscript returns empty string, peer is permitted." >&2
	echo "	If permitted, the command is invoked with args." >&2
	echo "	By default a permission is granted for $ALLOW seconds." >&2
	echo "	A wrong password penalty is $BLOCK seconds." >&2
	echo "	Checkscript can utilize $IP and $STAMP (filename)." >&2
	exit 1
fi

log()
{
printf '[%s][%05d] - %s\n' "$SOCKLINGER_NR" "$PPID" "$*" >&2
}

ex()
{
log "$*"
exit 1
}

no()
{
echo NO
ex "NOT permitted peer $IP: $*"
}

[ -z "$SOCKLINGER_PEER" ] &&
ex "Only use this from socklinger!"

IP="$SOCKLINGER_PEER"
case "$SOCKLINGER_PEER" in
*/* | *..*)	ex "illegal peer $SOCKLINGER_PEER";;
\[*\]:*)	IP="`expr "$IP" : '\[\(.*\)\]:[^:]*$'`";;
*:*)		IP="`expr "$IP" : '\(.*\):[^:]*$'`";;
esac

# Calculate timestamp window in which everything is OK
NOW="`date +%s`"
MAX="`expr "$NOW" + "$ALLOW"`"

# Hunt for the peer name
DIR="$TMP/portcullis/$LOGNAME/sock-$SOCKLINGER_SOCK"
STAMP="$DIR/peer-$IP.stamp"

# Remove permission if stale
# And set the deny flag according to permission
deny=""
[ -s "$STAMP" ] &&
if	! read valid deny < "$STAMP" ||
	[ "$NOW" -gt "$valid" ] ||
	[ "$MAX" -lt "$valid" ]
then
	if rm -f "$STAMP" 2>/dev/null
	then
		log "stale permission for peer $IP"
		rmdir "$DIR" 2>/dev/null
	else
		log "cannot revoke stale permission for peer $IP"
		deny="stale permission"
	fi
fi

if [ -s "$STAMP" ]
then
	[ -n "$deny" ] && no "$deny"
	log "permission valid for peer $IP"
else
	# Run the checkscript:
	#
	# This script returns:
	# 1			PEER NOT ALLOWED
	# 0 + empty string:	peer allowed
	# 0 + some STRING:	ask for this PASSWORD
	export IP STAMP
	try="`/bin/sh -c "$1"`" || no "permission denied"

	if [ -n "$try" ]
	then
		echo "Enter password:"
		read -t10 pw || no "timeout"
		if [ ".$try" != ".$pw" ] && [ ".$try" != ".$pw" ]
		then
			[ 0 -lt "$BLOCK" ] && echo "`expr "$NOW" + "$BLOCK"` blocked" > "$STAMP"
			no "invalid password"
		fi
		mkdir -p "$DIR" || no "cannot create dir"
		chmod 700 "$DIR" || no "cannot set dir mode"
		echo "$MAX" > "$STAMP" &&
		echo OK
 		log "permission granted to peer $IP"
		# Now throw out the client,
		# next time it gets the service.
		exit 0
	fi
	# Fall through service
	log "directly permitted peer $IP"
fi

shift
exec "$@"
