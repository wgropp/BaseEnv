bin_PROGRAMS = pingpong vectest
pingpong_SOURCES = pingpong.c
LDADD = ../../libbenv.la

vectest_SOURCES = vectest.c

# Automake doesn't support a conditional setting of xx_LINK.
# Need to add something like --Wno-override inorder to (even conditionally)
# redefine CCLD. You'd think -Wno-override would be the correct option,
# but it seems to need no-syntax
AUTOMAKE_OPTIONS=-Wno-override -Wno-syntax
if HAVE_CUDA
# Using libtool with nvcc is difficult. Just use a regular library
LDADD += ../../libbenvcuda.a @CCCUDAFLAGS@ -lcuda -lcudart
# Override the linker for pingpong (This should really be a symbol for
# a value set by configure). See AUTOMAKE_OPTIONS above that are needed
# to suppress warnings caused by doing this
CCLD = mpic++
endif
if HAVE_HIP
LDADD += ../../libbenvhip.a -L$HIP_INSTALL_PATH/lib64 -lhip
CCLD = mpicc
endif

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
