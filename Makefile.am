ACLOCAL_AMFLAGS = -I confdb
SUBDIRS = libsrc . src
DIST_SUBDIRS = $(SUBDIRS) doc

lib_LTLIBRARIES = libbenv.la libbenvcartshim.la
libbenv_la_LIBADD = libsrc/util/libbenvutil.la libsrc/cart/libbenvcart.la \
	libsrc/topometrics/libbenvtopomet.la \
	libsrc/timearray/libbenvta.la \
	libsrc/hwdesc/libbenvhwdesc2.la
libbenv_la_SOURCES =

if HAVE_CUDA
lib_LIBRARIES = libbenvcuda.a
libbenvcuda_a_LIBADD = libsrc/util/memcuda.o
libbenvcuda_a_SOURCES =
endif
if HAVE_HIP
lib_LIBRARIES = libbenvhip.a
libbenvhip_a_LIBADD = libsrc/util/memhip.o
libbenvhip_a_SOURCES =
endif

libbenvcartshim_la_LIBADD = libsrc/cart/libbenvcartshim.la
libbenvcartshim_la_SOURCES =

# nodeinfo might be removed
# libsrc/nodeinfo/libbenvninfo.la

# Extensions to the automake Makefile to support use of doctext to
# create documentation
include $(abs_top_srcdir)/Makefile.doc

# The documentation program appends to baseenv.cit, so we must remove it
# before building the documentation.
mandoc:
	@for dir in $(SUBDIRS) ; do\
	    echo "Making mandoc in $$dir ..." ; \
	    if [ "$$dir" = "." ] ; then continue ; fi ; \
	    (cd $$dir && $(MAKE) mandoc) ; \
	done
	$(MAKE) mandoc-local
mandoc-local:
man:
	$(MAKE) mandoc
htmldoc:
	rm -f $(htmldoc_path)/baseenv.cit
	@for dir in $(SUBDIRS) ; do\
	    if [ "$$dir" = "." ] ; then continue ; fi ; \
	    echo "Making htmldoc in $$dir ..." ; \
	    (cd $$dir && $(MAKE) htmldoc) ; \
	done
	$(MAKE) htmldoc-local

htmldoc-local:
	maint/createhtmlindex
www:
	$(MAKE) htmldoc
texdoc:
	@for dir in $(SUBDIRS) ; do\
	    if [ "$$dir" = "." ] ; then continue ; fi ; \
	    echo "Making texdoc in $$dir ..." ; \
	    (cd $$dir && $(MAKE) texdoc) ; \
	done
	$(MAKE) texdoc-local

texdoc-local:

# Add the generated man and web documentation pages
# FIXME: Ensure that the doc pages have been built
EXTRA_DIST = Makefile.doc man www runscripts include
