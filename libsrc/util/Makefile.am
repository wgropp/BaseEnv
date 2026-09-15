AM_CPPFLAGS = -I../../include
noinst_LTLIBRARIES = libbenvutil.la
libbenvutil_la_SOURCES = getsizes.c seq.c mstring.c fname.c misc.c collchk.c \
	ntest.c mem.c memimpl.h dbg.c dbgmpi.c mpimisc.c collprint.c ilist.c
if HAVE_CUDA
# Using libtool with nvcc is difficult. Just build a regular object
# Building a regular object by itself is hard. Build a library
noinst_LIBRARIES = libbenvcuda.a
libbenvcuda_a_SOURCES = memcuda.cu

# Build for CUDA code
# Use (nearly) the same flags as for COMPILE
.cu.o:
	$(NVCC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	$(CPPFLAGS) -DUSING_NVCC $(AM_CFLAGS) $(NVCCFLAGS) -c $<
endif
if HAVE_HIP
# Need to use hipcc to compile this (similar to using nvcc for cuda)
noinst_LIBRARIES = libbenvhip.a
libbenvhip_a_SOURCES = memhip.hip

# Build for HIP code
# Use (nearly) the same flags as for COMPILE
.hip.o:
	$(HIPCC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	$(CPPFLAGS) -DUSING_HIPCC $(AM_CFLAGS) $(HIPCCFLAGS) -c $<
endif

DIST_SUBDIRS = tests

SUFFIXES = .cu


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
