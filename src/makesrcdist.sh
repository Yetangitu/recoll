#!/bin/sh
# @(#$Id: makesrcdist.sh,v 1.7 2006-01-04 11:33:44 dockes Exp $  (C) 2005 J.F.Dockes
# A shell-script to make a recoll source distribution

#set -x

TAR=/usr/bin/tar
 
targetdir=${targetdir-/tmp}
dotag=${dotag-yes}

if test ! -d qtgui;then
    echo "Should be executed in the master recoll directory"
    exit 1
fi

version=`cat VERSION`
versionforcvs=`echo $version | sed -e 's/\./_/g'`

topdir=$targetdir/recoll-$version
if test ! -d $topdir ; then
    mkdir $topdir || exit 1
else 
    echo "Removing everything under $topdir Ok ? (y/n)"
    read rep 
    if test $rep = 'y';then
    	rm -rf $topdir/*
    fi
fi

################################### Documentation
###### Html doc
RECOLLDOC=${RECOLLDOC:=doc/user}
(cd $RECOLLDOC;make) || exit 1

###### Text Doc
chmod +w README INSTALL
cat <<EOF > README

More documentation can be found in the doc/ directory or at http://www.recoll.org


EOF
cat <<EOF > INSTALL

More documentation can be found in the doc/ directory or at http://www.recoll.org


EOF

echo "Dumping html documentation to text files"
links -dump ${RECOLLDOC}/usermanual.html >> README
links -dump ${RECOLLDOC}/rcl.install.html >> INSTALL

cvs commit -m '' README INSTALL

# Clean up this dir and copy the dist-specific files 
make clean
yes | clean.O
$TAR chfX - excludefile .  | (cd $topdir;$TAR xf -)

CVSTAG="RECOLL-$versionforcvs"
[ $dotag = "yes" ] && cvs tag -F $CVSTAG .

out=recoll-$version.tar.gz
(cd $targetdir ; \
    $TAR chf - recoll-$version | \
    	gzip > $out)
echo "$targetdir/$out created"
