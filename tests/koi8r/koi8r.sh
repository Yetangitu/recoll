#!/bin/sh

topdir=`dirname $0`/..
. $topdir/shared.sh

initvariables $0

recollq 'внешнии' > $mystdout 2> $mystderr

diff -w ${myname}.txt $mystdout > $mydiffs 2>&1

checkresult
