#!/bin/bash -e

TMPFILE=test_installwatch.sh_tmpfile
{
cat ../src/libinstallwatch.la
echo "echo \$libdir/\$dlname"
} | sh > $TMPFILE

LIBNAME=`cat $TMPFILE`

if [ "X`uname`" = "XDarwin" ]; then
	ENV="DYLD_INSERT_LIBRARIES=\"$LIBNAME\" DYLD_FORCE_FLAT_NAMESPACE=1"
else
	ENV="LD_PRELOAD=\"$LIBNAME\""
fi

eval "$ENV ./test_installwatch \"$LIBNAME\""
