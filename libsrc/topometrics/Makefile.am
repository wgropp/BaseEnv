noinst_LTLIBRARIES = libbenvtopomet.la
libbenvtopomet_la_SOURCES = topometrics.c
DIST_SUBDIRS =

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
