#!/bin/sh
# Run this to generate all the initial makefiles, etc.

DIE=0
package=freeciv
srcfile=client/civclient.h

# Uncomment the line below to debug this file
#DEBUG=defined

USE_NLS=yes

# Leave out NLS checks
for NAME in $@ ; do
  if test "x$NAME" = "x--disable-nls"; then 
    echo "+ nls checks disabled"
    USE_NLS=no
    break
  fi
done


debug ()
# print out a debug message if DEBUG is a defined variable
{
  if test ! -z "$DEBUG"
  then
    echo "DEBUG: $1"
  fi
}

version_check ()
# check the version of a package
# first argument : package name (executable)
# second argument : source download url
# rest of arguments : major, minor, micro version
{
  EXACT_VERSION=$1
  PACKAGE=$2
  URL=$3
  MAJOR=$4
  MINOR=$5
  MICRO=$6

  WRONG=

  debug "major $MAJOR minor $MINOR micro $MICRO"
  VERSION=$MAJOR
  if test ! -z "$MINOR"; then VERSION=$VERSION.$MINOR; else MINOR=0; fi
  if test ! -z "$MICRO"; then VERSION=$VERSION.$MICRO; else MICRO=0; fi

  debug "version $VERSION"
  if [ "$EXACT_VERSION" -eq 0 ]; then
    echo "+ checking for $PACKAGE >= $VERSION ... " | tr -d '\n'
  else
    echo "+ checking for $PACKAGE == $VERSION ... " | tr -d '\n'
  fi

  ($PACKAGE --version) < /dev/null > /dev/null 2>&1 || 
  {
	echo
	echo "You must have $PACKAGE installed to compile $package."
	echo "Download the appropriate package for your distribution,"
	echo "or get the source tarball at $URL"
	return 1
  }
  # the following line is carefully crafted sed magic
  pkg_version=`$PACKAGE --version|head -n 1|sed 's/^[a-zA-z\.\ ()]*//;s/ .*$//'`
  debug "pkg_version $pkg_version"
  pkg_major=`echo $pkg_version | cut -d. -f1`
  pkg_minor=`echo $pkg_version | sed s/-.*// | cut -d. -f2`
  pkg_micro=`echo $pkg_version | sed s/-.*// | cut -d. -f3`
  test -z "$pkg_minor" && pkg_minor=0
  test -z "$pkg_micro" && pkg_micro=0

  debug "found major $pkg_major minor $pkg_minor micro $pkg_micro"

  #start checking the version
  if [ "$EXACT_VERSION" -eq 0 ]; then
    debug "version check >="
    if [ "$pkg_major" -lt "$MAJOR" ]; then
      WRONG=1
    elif [ "$pkg_major" -eq "$MAJOR" ]; then
      if [ "$pkg_minor" -lt "$MINOR" ]; then
        WRONG=1
      elif [ "$pkg_minor" -eq "$MINOR" -a "$pkg_minor" -lt "$MINOR" ]; then
        WRONG=1
      fi
    fi
  else
    debug "version check =="
    if [ "$MAJOR" -ne "$pkg_major" -o \
         "$MINOR" -ne "$pkg_minor" -o \
         "$MICRO" -ne "$pkg_micro" ]; then
      WRONG=1
    fi
  fi

  if test ! -z "$WRONG"; then
    echo "found $pkg_version, not ok !"
    echo
    if [ "$EXACT_VERSION" -eq 0 ]; then
      echo "You must have $PACKAGE $VERSION or greater to compile $package."
      echo "Get the latest version from <$URL>."
    else
      echo "You must have exactly $PACKAGE $VERSION to compile $package."
      echo "Get this version from <$URL>."
    fi
    return 1
  else
    echo "found $pkg_version, ok."
  fi
}

version_check 1 "autoconf" "ftp://ftp.gnu.org/pub/gnu/autoconf/" 2 13 || DIE=1
version_check 0 "automake" "ftp://ftp.gnu.org/pub/gnu/automake/" 1 4 || DIE=1
if [ "$USE_NLS" = "yes" ] ; then
  DIE2=0
  version_check 0 "xgettext" "ftp://ftp.gnu.org/pub/gnu/gettext/" 0 10 38 || DIE2=1
  version_check 0 "msgfmt" "ftp://ftp.gnu.org/pub/gnu/gettext/" 0 10 38 || DIE2=1
  if test "$DIE2" -eq 1; then
    echo 
    echo "You may want to use --disable-nls to disable NLS."
    echo "This will also remove the dependency for xgettext and msgfmt."
    DIE=1
  fi    
fi

if test "$DIE" -eq 1; then
	exit 1
fi

test -f $srcfile || {
	echo "You must run this script in the top-level $package directory"
	exit 1
}

if test -z "$*"; then
	echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
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

# now remove the cache, because it can be considered dangerous in this case
echo "+ removing config.cache ... "
rm -f config.cache

echo "+ running configure ... "
echo "using: $@"
echo

./configure "$@" || {
	echo
	echo "configure failed"
	exit 1
}

echo 
echo "Now type 'make' to compile $package."
