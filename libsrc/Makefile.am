SUBDIRS = util hwdesc cart topometrics timearray

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
htmldoc:
	rm -f $(htmldoc_path)/baseenv.cit
	@for dir in $(SUBDIRS) ; do\
	    if [ "$$dir" = "." ] ; then continue ; fi ; \
	    echo "Making htmldoc in $$dir ..." ; \
	    (cd $$dir && $(MAKE) htmldoc) ; \
	done
	$(MAKE) htmldoc-local

htmldoc-local:
texdoc:
	@for dir in $(SUBDIRS) ; do\
	    if [ "$$dir" = "." ] ; then continue ; fi ; \
	    echo "Making texdoc in $$dir ..." ; \
	    (cd $$dir && $(MAKE) texdoc) ; \
	done
	$(MAKE) texdoc-local

texdoc-local:

