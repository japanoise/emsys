#!/bin/sh
usage() {
    echo "$0 CC VERSION DISTNAME" >&2
}
if [ -z "$1" ]
then
    usage
    exit 1
fi
if [ -z "$2" ]
then
    usage
    exit 1
fi
if [ -z "$3" ]
then
    usage
    exit 1
fi

compiler="$1"
version="$2"
distname="$3"
progname="emsys"
archive="$progname-$distname-$version"

make clean
make CC="$compiler" VERSION="$version" || exit 1

echo ""
echo "Building complete, stripping"
echo "(one error expected, either you do or don't have a .exe)"
echo ""

strip -v "$progname"
strip -v "$progname".exe

echo ""
echo "Done, now build archive"
echo "(one error expected, either you do or don't have a .exe)"
echo ""

(
    mkdir -pv build/"$archive"
    cp -v "$progname" build/"$archive"
    cp -v "$progname".exe build/"$archive"
    cp -v "$progname".1 README.md LICENSE CHANGELOG build/"$archive"
    cd build || exit 1
    tar cvzf "$archive".tar.gz "$archive"
)
