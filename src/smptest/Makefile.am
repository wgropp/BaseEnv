bin_PROGRAMS = smptest
smptest_SOURCES = smptest.c
LDADD = ../../libbenv.la
# Note this will cause Automake to create smptest-smptest.o
smptest_CFLAGS = $(OPENMP_CFLAGS)

# Extensions to the automake Makefile to support use of doctext to
# create documentation
include $(abs_top_srcdir)/Makefile.doc
# Documentation targets
mandoc: $(mandoc_path1) $(SOURCES)
	$(DOCTEXT) $(DOCTEXT_MAN_OPTIONS) -mpath $(mandoc_path1) -ext 1 \
		$(SOURCES)
htmldoc: $(htmldoc_path1) $(SOURCES)
	$(DOCTEXT) $(DOCTEXT_HTML_OPTIONS) -mpath $(htmldoc_path1) \
		$(SOURCES)
texdoc: $(texdoc_path1) $(SOURCES)
	$(DOCTEXT) $(DOCTEXT_TEX_OPTIONS) -mpath $(texdoc_path1) \
		$(SOURCES)
