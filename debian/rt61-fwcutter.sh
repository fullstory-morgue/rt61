#!/bin/sh
set -e

bailout()
{
	[ "$1" -eq 0 ] && exit 0
	echo "ERROR $1: $2"
	exit "$1"
}

FW_DIR="$(pwd)"
TMP="$(mktemp -dp /tmp/ rt61-fwcutter.XXXXXX)" || bailout 1 "can't create temporary directory."

[ -z "$1" ] && bailout 2 "no input filename."
[ -r "$1" ] || bailout 3 "input tarball does not exist."

zcat "$1" > "$TMP/temporary.tar" || bzcat gzcat "$1" > "$TMP/temporary.tar"
[ "$?" -ne 0 ] && bailout 4 "input tarball $1 neither is neither valid gzip or bzip2."
cd "$TMP"
tar -xf temporary.tar || bailout 5 "couldn\'t extract firmware tarball."
find "$TMP" -iname rt2561.bin  >/dev/null || bailout 5 "rt2561.bin can\'t be found."
find "$TMP" -iname rt2561s.bin >/dev/null || bailout 6 "rt2561s.bin can\'t be found."
find "$TMP" -iname rt2661.bin  >/dev/null || bailout 8 "rt2661.bin can\'t be found."
find "$TMP" -iname \*\\.bin -exec cp {} "${FW_DIR}/" \;

