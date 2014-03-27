
bin_PROGRAMS =rasterlite_load \
	rasterlite_pyramid \
	rasterlite_topmost \
	rasterlite_grid \
	rasterlite_tool

INCLUDES = @CFLAGS@
INCLUDES += -I$(top_srcdir)/headers

rasterlite_load_SOURCES = rasterlite_load.c
rasterlite_pyramid_SOURCES = rasterlite_pyramid.c
rasterlite_topmost_SOURCES = rasterlite_topmost.c
rasterlite_grid_SOURCES = rasterlite_grid.c
rasterlite_tool_SOURCES = rasterlite_tool.c

LDADD = ../lib/.libs/librasterlite.a \
	@LIBSPATIALITE_LIBS@ @LIBPNG_LIBS@ \
        -lgeotiff -ltiff -ljpeg -lspatialite -lproj

MOSTLYCLEANFILES = *.gcna *.gcno *.gcda
