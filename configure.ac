#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(librasterlite, 1.1g, a.furieri @ lqt.it)
AC_LANG(C) 
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE 
AM_MAINTAINER_MODE
AM_CONFIG_HEADER(config.h)

# Checks for header files.
AC_CHECK_HEADERS(stdlib.h,, [AC_MSG_ERROR([cannot find stdlib.h, bailing out])])
AC_CHECK_HEADERS(stdio.h,,[AC_MSG_ERROR([cannot find stdio.h, bailing out])])
AC_CHECK_HEADERS(string.h,, [AC_MSG_ERROR([cannot find string.h, bailing out])])
AC_CHECK_HEADERS(memory.h,, [AC_MSG_ERROR([cannot find memory.h, bailing out])])
AC_CHECK_HEADERS(math.h,,[AC_MSG_ERROR([cannot find math.h, bailing out])])
AC_CHECK_HEADERS(float.h,,[AC_MSG_ERROR([cannot find float.h, bailing out])])
AC_CHECK_HEADERS(fcntl.h,,[AC_MSG_ERROR([cannot find fcntl.h, bailing out])])
AC_CHECK_HEADERS(inttypes.h,, [AC_MSG_ERROR([cannot find inttypes.h, bailing out])])
AC_CHECK_HEADERS(stddef.h,, [AC_MSG_ERROR([cannot find stddef.h, bailing out])])
AC_CHECK_HEADERS(stdint.h,, [AC_MSG_ERROR([cannot find stdint.h, bailing out])])
AC_CHECK_HEADERS(sys/time.h,, [AC_MSG_ERROR([cannot find sys/time.h, bailing out])])
AC_CHECK_HEADERS(unistd.h,, [AC_MSG_ERROR([cannot find unistd.h, bailing out])])
AC_CHECK_HEADERS(jpeglib.h,, [AC_MSG_ERROR([cannot find jpeglib.h, bailing out])])
AC_CHECK_HEADERS(jerror.h,, [AC_MSG_ERROR([cannot find jerror.h, bailing out])])

#
# testing GeoTiff headers
# on some systems they are located on -/include
# on other systems they are on -/include/geotiff
# or even on -/include/libgeotiff
#
# this patch was kindly contributed by Brad Hards
# 2011-09-02
#
AC_CHECK_HEADERS(geotiff/geotiff.h)
AC_CHECK_HEADERS(libgeotiff/geotiff.h)
AC_CHECK_HEADERS(geotiff.h)
if test x"$ac_cv_header_geotiff_geotiff_h" != x"yes" &&
    test x"$ac_cv_header_libgeotiff_geotiff_h" != x"yes" &&
    test x"$ac_cv_header_geotiff_h" != x"yes";
then
    AC_MSG_ERROR(['libgeotiff' is required but the header (geotiff.h) doesn't seem to be installed on this system])
fi 

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S 
AC_PROG_MAKE_SET 
AC_LIBTOOL_WIN32_DLL 
AC_PROG_LIBTOOL

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T 
AC_TYPE_SIZE_T 
AC_HEADER_TIME 
AC_STRUCT_TM 
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MEMCMP 
AC_FUNC_STAT 
AC_FUNC_STRFTIME
AC_CHECK_FUNCS([memset sqrt strcasecmp strerror strncasecmp strstr fdatasync ftruncate getcwd gettimeofday localtime_r memmove strerror])

# Checks for required libraries
AC_CHECK_LIB(z,inflateInit_,,AC_MSG_ERROR(['libz' is required but it doesn't seem to be installed on this system.]),-lm)
AC_CHECK_LIB(jpeg,jpeg_start_compress,,AC_MSG_ERROR(['libjpeg' is required but it doesn't seem to be installed on this system.]),-lm)
AC_CHECK_LIB(tiff,TIFFClientOpen,,AC_MSG_ERROR(['libtiff' is required but it doesn't seem to be installed on this system.]),-lm)
AC_CHECK_LIB(proj,pj_init_plus,,AC_MSG_ERROR(['libproj' is required but it doesn't seem to be installed on this system.]),-lm)
AC_CHECK_LIB(geotiff,GTIFSetFromProj4,,AC_MSG_ERROR(['libgeotiff' [>= v.1.2.5] is required but it doesn't seem to be installed on this system.]),-lm)

PKG_CHECK_MODULES([LIBSPATIALITE], [spatialite], , AC_MSG_ERROR(['libspatialite' is required but it doesn't seem to be installed on this system.]))
AC_SUBST(LIBSPATIALITE_CFLAGS)
AC_SUBST(LIBSPATIALITE_LIBS)

PKG_CHECK_MODULES([LIBPNG], [libpng], , AC_MSG_ERROR(['libpng' is required but it doesn't seem to be installed on this system.]))
AC_SUBST(LIBPNG_CFLAGS)
AC_SUBST(LIBPNG_LIBS)

#
#    --enable-gcov
#
AC_ARG_ENABLE(gcov, AC_HELP_STRING([--enable-gcov],[turn on code coverage analysis tools]))
if test "x$enable_gcov" = "xyes"; then
    dnl Check for -fprofile-arcs and -ftest-coverage option
    CFLAGS=$CFLAGS" -fprofile-arcs -ftest-coverage -g"
    GCOV_FLAGS="-lgcov"
fi


AC_CONFIG_FILES([Makefile \
		headers/Makefile \
		lib/Makefile \
		src/Makefile \
		test/Makefile \
		rasterlite.pc])

AC_OUTPUT
