if USE_OLD
# use with USE_OLD
noinst_PROGRAMS = scheddecode nodectest hwprint hwdebug ninfo tpolicy ptest \
	gentest genwnode cvartest
EXTRA_PROGRAMS = ninfo2 ptest2 ninfo3 nnn ninfopolicy
else
noinst_PROGRAMS = scheddecode nodectest hwprint hwdebug ninfo2 tpolicy ptest2 \
	gentest genwnode cvartest ninfo3 ninfopolicy
EXTRA_PROGRAMS = ninfo ptest
endif

scheddecode_SOURCES = scheddecode.c
nodectest_SOURCES = nodectest.c
hwprint_SOURCES = hwprint.c
hwdebug_SOURCES = hwdebug.c
ninfo_SOURCES = ninfo.c
ninfo2_SOURCES = ninfo2.c
ninfo3_SOURCES = ninfo3.c
ninfopolicy_SOURCES = ninfopolicy.c
tpolicy_SOURCES = tpolicy.c
ptest_SOURCES = ptest.c
ptest2_SOURCES = ptest2.c
gentest_SOURCES = gentest.c
genwnode_SOURCES = genwnode.c
cvartest_SOURCES = cvartest.c
nnn_SOURCES = nnn.c
AM_CPPFLAGS = -I../../../include
LDADD = ../../../libbenv.la
hwdebug_CPPFLAGS = -I..
tpolicy_CPPFLAGS = -I..

clean-local:
	rm -f *-stdout.txt *-stderr.txt ninfodetail.txt ninfo2detail.txt \
		ninfo-out.txt

testbuilds: scheddecode nodectest hwprint ninfo

# Tests do not contribute to the documentation, so only an empty target
# is included here
mandoc:
htmldoc:
