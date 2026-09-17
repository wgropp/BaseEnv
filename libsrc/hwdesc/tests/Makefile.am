noinst_PROGRAMS = scheddecode nodectest hwprint hwdebug ninfo2 tpolicy ptest2 \
	gentest genwnode cvartest ninfo3 ninfopolicy

scheddecode_SOURCES = scheddecode.c
nodectest_SOURCES = nodectest.c
hwprint_SOURCES = hwprint.c
hwdebug_SOURCES = hwdebug.c
ninfo2_SOURCES = ninfo2.c
ninfo3_SOURCES = ninfo3.c
ninfopolicy_SOURCES = ninfopolicy.c
tpolicy_SOURCES = tpolicy.c
ptest2_SOURCES = ptest2.c
gentest_SOURCES = gentest.c
genwnode_SOURCES = genwnode.c
cvartest_SOURCES = cvartest.c
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
