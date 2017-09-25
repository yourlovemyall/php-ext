dnl $Id$
dnl config.m4 for extension redis

PHP_ARG_ENABLE(myredis, whether to enable redis support,
dnl Make sure that the comment is aligned:
[  --enable-myredis           Enable redis support])

dnl PHP_ARG_ENABLE(redis-session, whether to enable sessions,
dnl [  --disable-redis-session      Disable session support], yes, no)

PHP_ARG_ENABLE(redis-igbinary, whether to enable igbinary serializer support,
[  --enable-redis-igbinary      Enable igbinary serializer support], no, no)


if test "$PHP_REDIS" != "no"; then

dnl  if test "$PHP_REDIS_SESSION" != "no"; then
dnl    AC_DEFINE(PHP_SESSION,1,[redis sessions])
dnl  fi

dnl Check for igbinary
  if test "$PHP_REDIS_IGBINARY" != "no"; then
    AC_MSG_CHECKING([for igbinary includes])
    igbinary_inc_path=""

    if test -f "$abs_srcdir/include/php/ext/igbinary/igbinary.h"; then
      igbinary_inc_path="$abs_srcdir/include/php"
    elif test -f "$abs_srcdir/ext/igbinary/igbinary.h"; then
      igbinary_inc_path="$abs_srcdir"
    elif test -f "$phpincludedir/ext/igbinary/igbinary.h"; then
      igbinary_inc_path="$phpincludedir"
    else
      for i in php php4 php5 php6; do
        if test -f "$prefix/include/$i/ext/igbinary/igbinary.h"; then
          igbinary_inc_path="$prefix/include/$i"
        fi
      done
    fi

    if test "$igbinary_inc_path" = ""; then
      AC_MSG_ERROR([Cannot find igbinary.h])
    else
      AC_MSG_RESULT([$igbinary_inc_path])
    fi
  fi

  AC_MSG_CHECKING([for redis igbinary support])
  if test "$PHP_REDIS_IGBINARY" != "no"; then
    AC_MSG_RESULT([enabled])
    AC_DEFINE(HAVE_REDIS_IGBINARY,1,[Whether redis igbinary serializer is enabled])
    IGBINARY_INCLUDES="-I$igbinary_inc_path"
    IGBINARY_EXT_DIR="$igbinary_inc_path/ext"
    ifdef([PHP_ADD_EXTENSION_DEP],
    [
      PHP_ADD_EXTENSION_DEP(redis, igbinary)
    ])
    PHP_ADD_INCLUDE($IGBINARY_EXT_DIR)
  else
    IGBINARY_INCLUDES=""
    AC_MSG_RESULT([disabled])
  fi

  PHP_NEW_EXTENSION(myredis, myredis.c library.c, $ext_shared)
fi
