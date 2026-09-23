AM_CPPFLAGS = -I../../include
noinst_LTLIBRARIES = libbenvhwdesc2.la
libbenvhwdesc2_la_SOURCES = hwdescutil2.c hwdescarg.c mpixsplit.c \
	hwdescparm.c hwdescprint.c nodeinfo2.c hwdescimpl2.h

SUBDIRS = tests

# See src/hwinfo for a sample program
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


