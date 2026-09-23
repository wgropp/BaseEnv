AUTOMAKE_OPTIONS = subdir-objects
check_PROGRAMS = ncarttest cartranktst factortst shimtest distdimstst
ncarttest_SOURCES = ncarttest.c
cartranktst_SOURCES = ../cartrank.c ../factor.c
cartranktst_CPPFLAGS = -DBUILD_TEST $(AM_CPPFLAGS)
factortst_SOURCES = ../factor.c
factortst_CPPFLAGS = -DBUILD_FACTOR_TEST $(AM_CPPFLAGS)
shimtest_SOURCES = shimtest.c
shimtestf_SOURCES = shimtestf.f90
shimtest_LDADD = ../../../libbenvcartshim.la ../../../libbenv.la
distdimstst_SOURCES = distdimstst.c
AM_CPPFLAGS = -I../../../include -I..
LDADD = ../../../libbenv.la
EXTRA_PROGRAMS = shimtestf

TESTS = ncarttest.sh factortst.sh distdimstst.sh shimtest.sh
# cartranktst.sh shimtestf.sh

# Tests do not contribut to the documentation, so only a empty target
# is included here
mandoc:
htmldoc:
