bin_PROGRAMS = meshio ctestinput
SUBDIRS = stencil
meshio_SOURCES = meshio.c
ctestinput_SOURCES = ctestinput.c
LDADD = ../../libbenv.la

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
