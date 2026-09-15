noinst_PROGRAMS = getsizestest ilisttest cprinttest
getsizestest_SOURCES = getsizestest.c
ilisttest_SOURCES = ilisttest.c
cprinttest_SOURCES = cprinttest.c

AM_CPPFLAGS = -I../../../include
LDADD = ../../../libbenv.la

if HAVE_CUDA
noinst_PROGRAMS += memcudatest
# Using libtool with nvcc is difficult. Just use a regular library
LDADD += ../../../libbenvcuda.a -lcuda -lcudart
memcudatest_SOURCES = memcudatest.c
# Override the linker for memcudatest
memcudatest_LINK= $(LIBTOOL) $(AM_V_lt) --tag=CC $(AM_LIBTOOLFLAGS) \
	$(LIBTOOLFLAGS) --mode=link mpic++ $(AM_CFLAGS) $(CFLAGS) \
	$(AM_LDFLAGS) $(LDFLAGS) -o $@
endif

testbuilds: getsizestest ilisttest

# Tests do not contribut to the documentation, so only a empty target
# is included here
mandoc:
htmldoc:
