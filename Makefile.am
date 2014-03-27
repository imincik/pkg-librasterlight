ACLOCAL_AMFLAGS = -I m4

SUBDIRS = headers lib src test

AUTOMAKE_OPTIONS = dist-zip

EXTRA_DIST = Makefile-static-MinGW Makefile-static-Linux \
	Makefile-static-MacOsX makefile.vc nmake.opt 

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = rasterlite.pc

coverage-init:
	lcov --directory lib --capture --initial --output-file librasterlite_cov.info

coverage::
	lcov --directory lib --output-file librasterlite_cov.info --capture
	genhtml -o covresults librasterlite_cov.info

MOSTLYCLEANFILES = librasterlite_cov.info
