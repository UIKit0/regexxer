## $Id$
##
## Copyright (c) 2002  Daniel Elstner  <daniel.elstner@gmx.net>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License VERSION 2 as
## published by the Free Software Foundation.  You are not allowed to
## use any other version of the license; unless you got the explicit
## permission from the author to do so.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


## PCRE_CHECK_VERSION(min_version)
## Run pcre-config to determine the libpcre version number and
## bail out if it is insufficient.  Also retrieve the necessary
## compiler flags and store them in PCRE_CFLAGS rpt. PCRE_LIBS.
##
AC_DEFUN([PCRE_CHECK_VERSION],
[
AC_ARG_VAR([PCRE_CONFIG], [path to pcre-config script])
AC_PATH_PROG([PCRE_CONFIG], [pcre-config], [not found])
if test "x$PCRE_CONFIG" = "xnot found"
then
{
AC_MSG_ERROR([[
*** pcre-config is missing.  Please install your distribution's
*** libpcre development package and then try again.
]])
}
fi

AC_MSG_CHECKING([[for PCRE >= ]$1])
pcre_version=`$PCRE_CONFIG --version`
pcre_version_ok="no"
expr "$pcre_version" '>=' "[$1]" >/dev/null 2>&1 && pcre_version_ok="yes"
AC_MSG_RESULT([${pcre_version_ok}])

if test "x$pcre_version_ok" = "xno"
then
{
AC_MSG_ERROR([[
*** libpcre ]$1[ or higher is required, but you only have
*** version ${pcre_version} installed.  Please upgrade and try again.
]])
}
fi

AC_MSG_CHECKING([[PCRE_CFLAGS]])
PCRE_CFLAGS=`$PCRE_CONFIG --cflags`
AC_MSG_RESULT([${PCRE_CFLAGS}])
AC_SUBST([PCRE_CFLAGS])

AC_MSG_CHECKING([[PCRE_LIBS]])
PCRE_LIBS=`$PCRE_CONFIG --libs`
AC_MSG_RESULT([${PCRE_LIBS}])
AC_SUBST([PCRE_LIBS])
])


## PCRE_CHECK_UTF8()
## Run a test program to determine whether PCRE was compiled with
## UTF-8 support.  If it wasn't, bail out with an error message.
##
AC_DEFUN([PCRE_CHECK_UTF8],
[
AC_MSG_CHECKING([[whether PCRE was compiled with UTF-8 support]])
AC_LANG_PUSH(C)
pcre_saved_CFLAGS="$CFLAGS"
pcre_saved_LIBS="$LIBS"
CFLAGS="$CFLAGS $PCRE_CFLAGS"
LIBS="$LIBS $PCRE_LIBS"

AC_TRY_RUN(
[
#include <stdio.h>
#include <stdlib.h>
#include <pcre.h>

int main(int argc, char** argv)
{
  const char* errptr = NULL;
  int erroffset = 0;

  if(pcre_compile(".", PCRE_UTF8, &errptr, &erroffset, NULL))
    exit(0);

  fprintf(stderr, "%s\n", errptr);
  exit(1);

  return 0;
}
],
[pcre_supports_utf8="yes"],
[pcre_supports_utf8="no"],
[pcre_supports_utf8="cross compile: assuming yes"]
)

CFLAGS="$pcre_saved_CFLAGS"
LIBS="$pcre_saved_LIBS"
AC_LANG_POP(C)
AC_MSG_RESULT([${pcre_supports_utf8}])

if test "x$pcre_supports_utf8" = "xno"
then
{
AC_MSG_ERROR([[
*** Sorry, the libpcre installed on your system doesn't support
*** UTF-8 encoding.  Please install a libpcre package which includes
*** support for UTF-8, or compile PCRE from source code.
]])
}
fi
])

