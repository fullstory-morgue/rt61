#!/bin/sh
set -e

DRIVER="RT61_Linux_STA_Drv1.1.0.0.tar.gz"
URL="http://www.ralinktech.com/ralink/data/$DRIVER"

bailout()
{
	[ "$1" -eq 0 ] && exit 0
	echo "ERROR $1: $2"
	exit "$1"
}

TMP="$(mktemp -dp /tmp/ rt61-firmware.XXXXXX)" || bailout 1 "can't create temporary directory."
[ -x /usr/bin/wget ] && DL=wget
[ -x /usr/bin/curl ] && DL="curl -o $dname"

cd "$TMP"
$DL "$URL"

rt61-fwcutter "$dname"

mkdir -p /lib/firmware
for i in *.bin; do
        mv "$i" "/lib/firmware/$i";
done

rm -rf "$TMP"

