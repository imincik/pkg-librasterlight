check_PROGRAMS = check_version \
		check_openclose \
		check_badopen \
		check_metadata \
		check_resolution \
		check_colours \
		check_rastergen

AM_CFLAGS = -I$(top_srcdir)/headers
AM_LDFLAGS = -L../lib @LIBSPATIALITE_LIBS@  -lrasterlite -lm $(GCOV_FLAGS)

TESTS = $(check_PROGRAMS)

MOSTLYCLEANFILES = *.gcna *.gcno *.gcda

EXTRA_DIST = globe.sqlite jpeg50ref.jpg
