noinst_LTLIBRARIES = libbenvcart.la libbenvcartshim.la
#lib_LTLIBRARIES = libbenvcartshim.la
libbenvcart_la_SOURCES = ncart.c cartrepl.c meshsup.c subarrays.c cartrank.c \
	cartimpl.h cartimplm.h factor.c decomp.c

# The shim library must be separate because it overrides the MPI routines
# (that is, it defined MPI_Cart_create etc., making the MPI library
# versions of those inaccessible when a program links with a library
# containing the shims)
libbenvcartshim_la_SOURCES = ncartshim.c
# The shims for Fortran depend on the availabily of a Fortran compiler
if INCLUDESHIMLIB
libbenvcartshim_la_SOURCES += ncartshimf.c
endif

DIST_SUBDIRS = tests


testbuilds:
	(cd tests && $(MAKE) testbuilds)
clean-local:
	(cd tests && $(MAKE) clean)

# Extensions to the automake Makefile to support use of doctext to
# create documentation
include $(abs_top_srcdir)/Makefile.doc
# Documentation targets
mandoc: $(mandoc_path3) $(SOURCES)
	$(DOCTEXT) $(DOCTEXT_MAN_OPTIONS) -mpath $(mandoc_path3) -ext 3 \
		$(abs_top_srcdir)/doc/common.txt $(SOURCES)
htmldoc: $(htmldoc_path3) $(SOURCES)
	$(DOCTEXT) $(DOCTEXT_HTML_OPTIONS) -mpath $(htmldoc_path3) \
		$(abs_top_srcdir)/doc/common.txt $(SOURCES)
texdoc: $(texdoc_path3) $(SOURCES)
	$(DOCTEXT) $(DOCTEXT_TEX_OPTIONS) -mpath $(texdoc_path3) \
		$(abs_top_srcdir)/doc/common.txt $(SOURCES)
