#!/bin/sh
# Run this to generate all the initial makefiles, etc.

# This is a kludge to make Gentoo behave and select the 
# correct version of automake to use.
WANT_AUTOMAKE=1.8
export WANT_AUTOMAKE

DIE=0
package=freeciv
srcfile=client/civclient.h

SRCDIR=`dirname $0`
BUILDDIR=`pwd`

# Uncomment the line below to debug this file
#DEBUG=defined

FC_USE_NLS=yes
FC_USE_NEWAUTOCONF=yes
FC_HELP=no

# Leave out NLS checks
for NAME in $@ ; do
  if [ "x$NAME" = "x--help" ]; then 
    FC_HELP=yes
  fi
  if [ "x$NAME" = "x--disable-nls" ]; then 
    echo "! nls checks disabled"
    FC_USE_NLS=no
  fi
  if [ "x$NAME" = "x--disable-autoconf2.52" ]; then 
    echo "! forcing old autoconf configuration"
    FC_USE_NEWAUTOCONF=no
  else
    FC_NEWARGLINE="$FC_NEWARGLINE $NAME"
  fi
done

debug ()
# print out a debug message if DEBUG is a defined variable
{
  if [ ! -z "$DEBUG" ]; then
    echo "DEBUG: $1"
  fi
}

version_check ()
# check the version of a package
# first argument : complain ('1') or not ('0')
# second argument : package name (executable)
# third argument : source download url
# rest of arguments : major, minor, micro version
{
  COMPLAIN=$1
  PACKAGE=$2
  URL=$3
  MAJOR=$4
  MINOR=$5
  MICRO=$6

  WRONG=

  debug "major $MAJOR minor $MINOR micro $MICRO"
  VERSION=$MAJOR
  if [ ! -z "$MINOR" ]; then VERSION=$VERSION.$MINOR; else MINOR=0; fi
  if [ ! -z "$MICRO" ]; then VERSION=$VERSION.$MICRO; else MICRO=0; fi

  debug "version $VERSION"
  echo "+ checking for $PACKAGE >= $VERSION ... " | tr -d '\n'

  ($PACKAGE --version) < /dev/null > /dev/null 2>&1 || 
  {
    echo
    echo "You must have $PACKAGE installed to compile $package."
    echo "Download the appropriate package for your distribution,"
    echo "or get the source tarball at $URL"
    return 1
  }
  # the following line is carefully crafted sed magic
  pkg_version=`$PACKAGE --version|head -n 1|sed 's/([^)]*)//g;s/^[a-zA-Z\.\ \-]*//;s/ .*$//'`
  debug "pkg_version $pkg_version"
  pkg_major=`echo $pkg_version | cut -d. -f1`
  pkg_minor=`echo $pkg_version | sed s/[-,a-z,A-Z].*// | cut -d. -f2`
  pkg_micro=`echo $pkg_version | sed s/[-,a-z,A-Z].*// | cut -d. -f3`
  [ -z "$pkg_minor" ] && pkg_minor=0
  [ -z "$pkg_micro" ] && pkg_micro=0

  debug "found major $pkg_major minor $pkg_minor micro $pkg_micro"

  #start checking the version
  if [ "$pkg_major" -lt "$MAJOR" ]; then
    WRONG=1
  elif [ "$pkg_major" -eq "$MAJOR" ]; then
    if [ "$pkg_minor" -lt "$MINOR" ]; then
      WRONG=1
    elif [ "$pkg_minor" -eq "$MINOR" -a "$pkg_micro" -lt "$MICRO" ]; then
      WRONG=1
    fi
  fi

  if [ ! -z "$WRONG" ]; then
   echo "found $pkg_version, not ok !"
   if [ "$COMPLAIN" -eq "1" ]; then
     echo
     echo "You must have $PACKAGE $VERSION or greater to compile $package."
     echo "Get the latest version from <$URL>."
     echo
   fi
   return 1
  else
    echo "found $pkg_version, ok."
  fi
}

# Chdir to the srcdir, then run auto* tools.
cd $SRCDIR

[ -f $srcfile ] || {
  echo "Are you sure $SRCDIR is a valid source directory?"
  exit 1
}

# We need to move configure.in out of the way in case we have autoconf
# > 2.13 since that would cause lots of spurious warnings. Same goes
# for acconfig.h
[ -f configure.in ] && {
  mv configure.in configure.old
}

[ -f acconfig.h ] && {
  mv acconfig.h acconfig.old
}

# x.252 is autoconf 2.52 compatible version of this macro, while x.213 is
# the original autoconf 2.13 version; we must suppose 2.52 by default here
cp m4/x.252 m4/x.m4

if [ "$FC_USE_NEWAUTOCONF" = "yes" -a -f configure.old2 ]; then
  mv configure.old2 configure.ac
fi
if [ "$FC_USE_NEWAUTOCONF" = "no" -a -f configure.ac ]; then
  mv configure.ac configure.old2
fi

if [ "$FC_USE_NEWAUTOCONF" = "yes" ] \
 && version_check 0 "autoconf" "ftp://ftp.gnu.org/pub/gnu/autoconf/" 2 52
then
  FC_AUTOCONF=new
else
  FC_AUTOCONF=old
  mv configure.old configure.in
  mv acconfig.old acconfig.h
  cp m4/x.213 m4/x.m4
  version_check 1 "autoconf" "ftp://ftp.gnu.org/pub/gnu/autoconf/" 2 13 || DIE=1
fi

version_check 1 "automake" "ftp://ftp.gnu.org/pub/gnu/automake/" 1 4 || DIE=1
if [ "$FC_USE_NLS" = "yes" ]; then
  DIE2=0
  version_check 1 "xgettext" "ftp://ftp.gnu.org/pub/gnu/gettext/" 0 10 36 || DIE2=1
  version_check 1 "msgfmt" "ftp://ftp.gnu.org/pub/gnu/gettext/" 0 10 36 || DIE2=1
  if [ "$DIE2" -eq 1 ]; then
    echo 
    echo "You may want to use --disable-nls to disable NLS."
    echo "This will also remove the dependency for xgettext and msgfmt."
    DIE=1
  fi
fi

if [ "$DIE" -eq 1 ]; then
  exit 1
fi

if [ "$FC_AUTOCONF" = "new" ]; then
  echo "+ using new autoconf configuration (use --disable-autoconf2.52 to use old)"
fi

echo "+ creating acinclude.m4"
cat m4/*.m4 > acinclude.m4

echo "+ running aclocal ..."
aclocal $ACLOCAL_FLAGS || {
  echo
  echo "aclocal failed - check that all needed development files are present on system"
  exit 1
}

echo "+ running autoheader ... "
autoheader || {
  echo
  echo "autoheader failed"
  exit 1
}
echo "+ running autoconf ... "
autoconf || {
  echo
  echo "autoconf failed"
  exit 1
}
echo "+ running automake ... "
automake -a -c || {
  echo
  echo "automake failed"
  exit 1
}

# Chdir back to the builddir before the configure step.
cd $BUILDDIR

# now remove the cache, because it can be considered dangerous in this case
echo "+ removing config.cache ... "
rm -f config.cache

echo "+ running configure ... "
echo
if [ -z "$FC_NEWARGLINE" ]; then
  echo "I am going to run ./configure with no arguments - if you wish "
  echo "to pass any to it, please specify them on the $0 command line."
else
  echo "using: $FC_NEWARGLINE"
fi
echo

$SRCDIR/configure $FC_NEWARGLINE || {
  echo
  echo "configure failed"
  exit 1
}

# Reverse changes to make tree sane
[ -f $SRCDIR/configure.old ] && { 
  mv $SRCDIR/configure.old $SRCDIR/configure.in 
}
[ -f $SRCDIR/configure.old2 ] && { 
  mv $SRCDIR/configure.old2 $SRCDIR/configure.ac 
}
[ -f $SRCDIR/acconfig.h ] && { 
  mv $SRCDIR/acconfig.h $SRCDIR/acconfig.old
}

# abort if we did --help
if [ "$FC_HELP" = "yes" ]; then
  exit 1
fi

echo 
echo "Now type 'make' to compile $package."

exit 0
