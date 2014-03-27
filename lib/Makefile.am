
AM_CFLAGS = $(GCOV_FLAGS)

INCLUDES = @CFLAGS@
INCLUDES += -I$(top_srcdir)/headers

lib_LTLIBRARIES = librasterlite.la 

librasterlite_la_SOURCES = \
     rasterlite_io.c \
     rasterlite_image.c \
     rasterlite_aux.c \
     rasterlite_quantize.c \
     rasterlite_gif.c \
     rasterlite_png.c \
     rasterlite_jpeg.c \
     rasterlite_tiff.c \
     rasterlite_version.c \
     rasterlite.c

librasterlite_la_LDFLAGS = -version-info 2:0:0 -no-undefined

librasterlite_la_LIBADD = @LIBSPATIALITE_LIBS@ @LIBPNG_LIBS@ \
	-lgeotiff -ltiff -ljpeg -lspatialite -lproj

MOSTLYCLEANFILES = *.gcna *.gcno *.gcda
