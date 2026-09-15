bin_PROGRAMS = stencil-cart-p2p
stencil_cart_p2p_SOURCES = stencilmain.c sweep.c halo-p2p.c stencil.h
LDADD = ../../../libbenv.la

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
