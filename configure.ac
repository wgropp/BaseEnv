# Autoconf requirements
AC_PREREQ([2.72])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_MACRO_DIRS([confdb])
AC_CONFIG_AUX_DIR([build-aux])

AC_INIT([BaseEnv],[0.1],[wgropp@illinois.edu],[BaseEnv],[wgropp.cs.illinois.edu/BaseEnv])
AM_INIT_AUTOMAKE

# information on the package
AC_CONFIG_HEADERS([include/benvconf.h])

dnl Fortran isn't needed for most parts of benv
dnl Default is to include Fortran
AC_ARG_ENABLE([fortran],[AS_HELP_STRING([--enable-fortran],
 [Support Fortran tools such as cart shims for Fortran programs])],,[enable_fortran=yes])

dnl CUDA support is optional
AC_ARG_ENABLE([cuda],[AS_HELP_STRING([--enable-cuda],
[Support CUDA, including working with memory on NVIDIA GPUs])],,[enable_cuda=no])
dnl Allow alternative paths for CUDA
# Setup custom CUDA paths
AC_ARG_WITH([cuda],
   [AS_HELP_STRING(
        [--with-cuda=PATH],
        [prefix where CUDA is installed @<:@default=auto@:>@])],
   [CUDA_INSTALL_PATH=$withval],
   [with_cuda=auto])
AC_ARG_WITH([cuda-sdk],
   [AS_HELP_STRING(
        [--with-cuda-sdk=PATH],
        [prefix where CUDA SDK is installed @<:@default=auto@:>@])],
   [CUDA_SDK_INSTALL_PATH=$withval],
   [with_cuda_sdk=auto])

dnl Placeholders for enabling HIP and SYCL
AC_ARG_ENABLE([hip],
   [AS_HELP_STRING(
   [--enable-hip],
   [Enable use of HIP for accelerator memory])],,[enable_hip=no])
AC_ARG_ENABLE([sycl],
   [AS_HELP_STRING(
   [--enable-sycl],
   [Enable use of SYCL for accelerator memory])],,[enable_sycl=no])

AC_ARG_WITH([hwlocdir],
   [AS_HELP_STRING([--with-hwlocdir=prefix],
                   [Include the hwloc hardware locality package installed in the given directory])],
   [hwlocdirset=1],[hwlocdirset=0])

dnl strict is only for GNU compilers and compilers that accept GNU warning
dnl options.  Note that we have to compensate for the idiotically named -Wall
dnl that is in no way even close to turning on all of the warnings.
AC_ARG_ENABLE([strict],
    [AS_HELP_STRING([--enable-strict],
    [])],
    [CFLAGS="$CFLAGS -Wall -Wextra -Wno-missing-field-initializers -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-parameter -Wno-unused-label -Wshadow -Wmissing-declarations -Wno-long-long -Wundef -Wno-endif-labels -Wpointer-arith -Wbad-function-cast -Wwrite-strings -Wno-sign-compare -Wold-style-definition -Wno-multichar -Wno-deprecated-declarations -Wpacked -Wnested-externs -Winvalid-pch -Wno-pointer-sign -Wno-format-zero-length -Wtype-limits -Werror-implicit-function-declaration"],)
export CFLAGS

AC_ARG_WITH([oldhwdesc],
	[AS_HELP_STRING([--with-oldhwdesc],
	[Use the old version of the hwdesc routines])],
	[useold=yes],[useold=no])

if test z"$useold" = "zyes" ; then
    AC_DEFINE([USE_OLD],1,[Define to use old version of hwdesc routines])
fi
AM_CONDITIONAL([USE_OLD],[test x$useold = xyes])

# checks for programs
dnl AC_PROG_MAKE_SET
dnl Add @SET_MAKE@ to Makefile.in/am

AC_PROG_CC
dnl What to do about MPICC, and how to build programs with MPICC and automake?
dnl Note that we can live without the Fortran compiler, but it is needed
dnl to build some tools, such as the cart shim library for Fortran
if test "$enable_fortran" = "yes" ; then
    AC_PROG_FC
    AC_FC_WRAPPERS
else
    FC=false
fi
# automake and autoconf don't handle conditional execution of PROG_CXX well,
# so we unconditionally look for the C++ compiler. If this causes a problem,
# comment this out
AC_PROG_CXX
AM_PROG_AR
dnl AC_PROG_RANLIB
dnl LT_INIT handles ranlib
LT_INIT([disable-shared])

dnl If CUDA enabled, test for the NVIDIA compilers and set various flags
dnl and paths
dnl First test for required programs
CUDA_PROG_EXT=""
if test "$enable_cuda" = "yes" ; then
    AC_CHECK_PROGS([NVCC],[nvcc],["--fail--"])
    if test "$NVCC" = "--fail--" ; then
        AC_MSG_WARN([could not find nvcc])
	enable_cuda=no
    fi
    dnl If enable_cuda still yes, set Makefile variables
    if test "$enable_cuda" = "yes" ; then
       NVCCFLAGS="$NVCCFLAGS"
       CUDA_CFLAGS="$CUDA_CFLAGS"
       if test "x$CUDA_INSTALL_PATH" = "x" -a "x$CUDA_HOME" != "x" ; then
           CUDA_INSTALL_PATH=$CUDA_HOME
       fi
       if test "x$CUDA_SDK_INSTALL_PATH" != "x" ; then
           CUDA_CFLAGS="$CUDA_CFLAGS -I$CUDA_SDK_INSTALL_PATH/C/common/inc/"
       fi
       CUDA_CFLAGS="$CUDA_CFLAGS -I$CUDA_INSTALL_PATH/include"
       CUDA_LDFLAGS="-L$CUDA_INSTALL_PATH/lib64"
       CUDA_LIBS="-lcuda -lcudart -lcufft"
       NVCCFLAGS="$NVCCFLAGS $CUDA_CFLAGS $CUDA_LDFLAGS $CUDA_LIBS"
       CUDA_PROG_EXT="-cuda"
       # For MPI programs, we may want to use CC, not NVCC. In that
       # case, we may need another lib path argument. See if we need one
       LIBSsave=$LIBS
       AC_SEARCH_LIBS([cudaGetDevice],[cudart],[cudaok=yes],[cudaok=no])
       LIBS=$LIBSsave
       if test "$cudaok" = "no" ; then
           # Try adding -L<cuda home>/lib64
	   unset ac_cv_search_cudaGetDevice
	   AC_MSG_RESULT([checking if $CC $CUDA_LDFLAGS can link with -lcudart])
	   LDFLAGSsave="$LDFLAGS"
	   LDFLAGS="$CUDA_LDFLAGS $LDFLAGS"
	   LIBSsave=$LIBS
           AC_SEARCH_LIBS([cudaGetDevice],[cudart],[cudaok=yes],[cudaok=no])
	   LIBS=$LIBSsave
	   if test "$cudaok" = "yes" ; then
	       AC_MSG_RESULT([use $CUDA_LDFLAGS with $CC])
	       CCCUDAFLAGS="$CUDA_LDFLAGS"
	       CUDALD=$CC
	       AC_SUBST(CCCUDAFLAGS)
           else
	       unset ac_cv_search_cudaGetDevice
	       LDFLAGS="$LDFLAGSsave"
	       # Try using the C++ compiler instead
	       AC_LANG_PUSH([C++])
	       AC_MSG_RESULT([checking if $CXX $CUDA_LDFLAGS can link with -lcudart])
	       LDFLAGS="$CUDA_LDFLAGS $LDFLAGS"
	       LIBSsave=$LIBS
               AC_SEARCH_LIBS([cudaGetDevice],[cudart],[cudaok=yes],[cudaok=no])
	       LIBS=$LIBSsave
	       if test "$cudaok" = "yes" ; then
	           CUDALD=$CXX
	           CCCUDAFLAGS="$CUDA_LDFLAGS"
	       else
	           # If that doesn't work, we're stuck
	           AC_MSG_RESULT([cannot link with cudart!])
	       fi
	       AC_LANG_POP
	   fi
	   LDFLAGS="$LDFLAGSsave"
       fi

       # May need either mpic++ or nvc++ to link, even if files compiled with
       # nvcc
       AC_SUBST(NVCC)
       AC_SUBST(NVCCFLAGS)
       AC_DEFINE([HAVE_CUDA],[1],[Defined if CUDA can be used])
       AC_SUBST(CUDA_PROG_EXT)
    fi
fi
AM_CONDITIONAL([HAVE_CUDA],[test x$enable_cuda = xyes])

# Placeholders for more complete tests
if test "$enable_hip" = "yes" ; then
    AC_CHECK_PROGS([HIPCC],[hipcc],["--fail--"])
    if test "$HIPCC" = "--fail--" ; then
        AC_MSG_WARN([could not find hipcc])
	enable_hip=no
    fi
    dnl If enable_cuda still yes, set Makefile variables
    if test "$enable_hip" = "yes" ; then
       # ? -D__HIP_ARCH_GFX90A__=1
       HIPCCFLAGS="$HIPCCFLAGS"
       HIP_CFLAGS="$HIP_CFLAGS"
       # FIXME: Need to ensure PATH variables are set before using
       HIP_CFLAGS="$HIP_CFLAGS -I$HIP_SDK_INSTALL_PATH/C/common/inc/"
       HIP_CFLAGS="$HIP_CFLAGS -I$HIP_INSTALL_PATH/include"
       # These are for Delta. Need a better way
       HIP_LDFLAGS="-L/opt/rocm/lib"
       HIP_LIBS="-lamdhip64 -lrccl"
       HIPCFLAGS="$HIPCCFLAGS $HIP_CFLAGS $HIP_LDFLAGS $HIP_LIBS"
       HIP_PROG_EXT=""
       AC_SUBST(HIPCC)
       AC_SUBST(HIPCCFLAGS)
       AC_DEFINE([HAVE_HIP],[1],[Defined if HIP can be used])
       AC_SUBST(HIP_PROG_EXT)
    fi
fi
AM_CONDITIONAL([HAVE_HIP],[test x$enable_hip = xyes])

if test "$enable_sycl" = "yes" ; then
   AC_DEFINE([HAVE_SYCL],[1],[Defined if SYCL can be used])
fi
AM_CONDITIONAL([HAVE_SYCL],[test x$enable_sycl = xyes])

# In case the documentation targets are used, find doctext and attempt to
# find the source for the doctext LaTeX style files.  Use "false" if
# doctext is not found
AC_PATH_PROG(DOCTEXT,[doctext],[false])
dnl export DOCTEXT
AC_SUBST(DOCTEXT)

# checks for libraries
AC_SEARCH_LIBS([MPI_Init],[mpi mpich],[hasmpi=yes],[hasmpi=no])
echo "LIBS now contains $LIBS"

# checks for header files
AC_CHECK_HEADERS([stdlib.h string.h mpi.h])

# Check for node/cpu information
# Linux getcpu and the more limited sched_getcpu.  Used for supporting
# node-awareness of socket and core.
AC_CHECK_HEADERS([utmpx.h])
AC_CHECK_FUNCS([getcpu sched_getcpu sched_getaffinity])
# sysctlbyname is a convenient interface to some configuration info
# on BSD. Linux, sadly, has nothing equivalent.
AC_CHECK_FUNCS([sysctlbyname])

# Check for NUMA information
AC_CHECK_HEADERS([numa.h])
AC_SEARCH_LIBS([numa_node_of_cpu],[numa],[hasnuma=yes],[hasnuma=no])
if test "$hasnuma" = "yes" ; then
    echo "LIBS now contains $LIBS"
    AC_DEFINE([HAVE_NUMA_NODE_OF_CPU],1,[Define if numa_node_of_cpu is available])
    AC_CHECK_FUNCS([numa_num_configured_nodes])
fi

# Look for hwloc, which provides extensive information on the node,
# particularly for locality (on systems that do not actively hide the
# information about processes and threads, such as OSX).
# First, see if there is an install directory for hwloc:
if test "$hwlocdirset" = 1 ; then
   # Test that the directories exist
   AC_MSG_CHECKING([whether hwloc directories exist])
   if test -d "$with_hwlocdir" -a -d "$with_hwlocdir/include" -a -d "$with_hwlocdir/lib" ; then
       AC_MSG_RESULT([yes])
       AM_CPPFLAGS="$AM_CPPFLAGS -I$with_hwlocdir/include"
       AM_LDFLAGS="$AM_LDFLAGS -L$with/hwlocdir/lib"
   else
       AC_MSG_RESULT([no - hwloc will not be used])
   fi
fi

AC_CHECK_HEADERS([hwloc.h])
LIBHWLOC=false
HWLOCLIB=
dnl We use CHECK_LIB because the hwloc should be included only where needed
dnl using SEARCH_LIBS unconditionally adds -lhwloc to LIBS
# It only makes sense to use for the hwloc library if we found the hwloc
# header.
has_hwloc=no
if test "$ac_cv_header_hwloc_h" = "yes" ; then
    AC_MSG_CHECKING([for hwloc routines in default libraries])
    AC_LINK_IFELSE([AC_LANG_CALL([], [hwloc_topology_init])],[LIBHWLOC=true],[])
    if test "$LIBHWLOC" = "false" ; then
        AC_MSG_RESULT([no])
        # Now look in -lhwloc
        AC_CHECK_LIB([hwloc],[hwloc_topology_init],[LIBHWLOC=true;HWLOCLIB=-lhwloc],[],[])
	if test "x$HWLOCLIB" != "x" ; then
	    LDFLAGS="$LDFLAGS $HWLOCLIB"
	fi
    else
        AC_MSG_RESULT([yes])
    fi
    if test "$LIBHWLOC" = "true" ; then
        AC_DEFINE([HAVE_HWLOC],[1],[Define if the hwloc library can be used])
        has_hwloc=yes
    fi
fi

AC_MSG_CHECKING([for /proc/cpuinfo])
if test -r /proc/cpuinfo ; then
    AC_MSG_RESULT([found])
    AC_DEFINE([HAVE_PROC_CPUINFO],1,[Define if /proc/cpuinfo is readable])
    has_cpuinfo=yes
else
    AC_MSG_RESULT([not found])
    has_cpuinfo=no
fi


# checks for types
# Some versions of Open MPI do not support MPI 4.0, but do support some parts
# of MPI 4.0 as well as Open MPI specific split types, which can be used for
# implementing some of the hardware information
# Test for
#  MPI_COMM_TYPE_HW_UNGUIDED
#     Open MPI 5 supports MPI_COMM_TYPE_HW_UNGUIDED but not all of MPI 4
#  OMPI_COMM_TYPE_SOCKET
#  OMPI_COMM_TYPE_NUMA
#  OMPI_COMM_TYPE_CORE
#  OMPI_COMM_TYPE_NODE
AC_MSG_CHECKING([whether MPI_COMM_TYPE_HW_UNGUIDED is defined])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include "mpi.h"],[int a=MPI_COMM_TYPE_HW_UNGUIDED;])],[hasMPIHWUnguided=yes],[hasMPIHWUnguided=no])
AC_MSG_RESULT([$hasMPIHWUnguided])

if test "$hasMPIHWUnguided" = "yes" ; then
    AC_DEFINE([HAS_MPI_COMM_TYPE_HW_UNGUIDED],[1],[Has MPI split type for HW])
fi

# Open MPI specific types
AC_MSG_NOTICE([checking whether Open MPI hardware splittypes are available])
AC_MSG_CHECKING([for OMPI_COMM_TYPE_NODE])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include "mpi.h"],[int a=OMPI_COMM_TYPE_NODE;])],[hasOMPINode=yes],[hasOMPINode=no])
AC_MSG_RESULT([$hasOMPINode])
AC_MSG_CHECKING([for OMPI_COMM_TYPE_SOCKET])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include "mpi.h"],[int a=OMPI_COMM_TYPE_SOCKET;])],[hasOMPISocket=yes],[hasOMPISocket=no])
AC_MSG_RESULT([$hasOMPISocket])
AC_MSG_CHECKING([for OMPI_COMM_TYPE_NUMA])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include "mpi.h"],[int a=OMPI_COMM_TYPE_NUMA;])],[hasOMPINUMA=yes],[hasOMPINUMA=no])
AC_MSG_RESULT([$hasOMPINUMA])
AC_MSG_CHECKING([for OMPI_COMM_TYPE_CORE])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include "mpi.h"],[int a=OMPI_COMM_TYPE_CORE;])],[hasOMPICore=yes],[hasOMPICore=no])
AC_MSG_RESULT([$hasOMPICore])

hasOMPISplittypes=no
if test "$hasOMPINode" = "yes" -a "$hasOMPISocket" = "yes" -a \
   "$hasOMPINUMA" = "yes" -a "$hasOMPICore" = "yes" ; then
    AC_DEFINE([HAS_OMPI_HWSPLITTYPES],[1],[Open MPI names for hw types])
    hasOMPISplittypes=yes
fi

# Some installations, and Cray MPI, are very old and support only MPI 3
# and the shared memory split type only
AC_MSG_CHECKING([whether MPI_COMM_TYPE_SHARED is defined])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include "mpi.h"],[int a=MPI_COMM_TYPE_SHARED;])],[hasMPIshared=yes],[hasMPIshared=no])
AC_MSG_RESULT([$hasMPIshared])
if test "$hasMPIshared" = "yes" ; then
   AC_DEFINE([HAS_MPI_COMM_TYPE_SHARED],[1],[Has MPI_COMM_TYPE_SHARED])
fi


# checks for structures
# checks for compiler characteristics
AC_C_CONST
AC_C_RESTRICT
AC_OPENMP

# checks for library functions
# MPI routines in MPI 4 but also implemented in some versions of Open MPI
# that do not implement all of MPI 4.
AC_CHECK_FUNCS([MPI_Info_get_string])
AC_CHECK_FUNCS([MPI_T_event_get_num MPI_T_event_get_info])

# Set conditional elements
CARTSHIMF=
if test "$enable_fortran" = "yes" ; then
   CARTSHIMF=ncartshimf.c
fi
AC_SUBST([CARTSHIMF])
AM_CONDITIONAL([INCLUDESHIMLIB],[test -n "$CARTSHIMF"])

# checks for system services

# Do a summary of results in terms of capabilities enabled
AC_MSG_NOTICE([Summary of features (with CPP name):
    Compilers:
       C                  $CC
       Fortran            $FC
    OpenMP                $ac_cv_prog_c_openmp
    Device Memory
       CUDA               $enable_cuda	HAVE_CUDA
       HIP                $enable_hip	HAVE_HIP
       SYCL               $enable_sycl	HAVE_SYCL
    MPI info:
       SHARED             $hasMPIshared	HAS_MPI_COMM_TYPE_SHARED
       UNGUIDED           $hasMPIHWUnguided	HAS_MPI_COMM_TYPE_HW_UNGUIDED
       OMPI Split types   $hasOMPISplittypes	HAS_OMPI_HWSPLITTYPES
    Node info:
       getcpu             $ac_cv_func_getcpu	HAVE_GETCPU
       sched_getcpu       $ac_cv_func_sched_getcpu	HAVE_SCHED_GETCPU
       sched_getaffinity  $ac_cv_func_sched_getaffinity	HAVE_SCHED_GETAFFINITY
       sysctlbyname       $ac_cv_func_sysctlbyname	HAVE_SYSCTLBYNAME
       numa.h functions   $hasnuma	HAVE_NUMA_NODE_OF_CPU HAVE_NUMA_NUM_CONFIGURED_NODES

       hwloc              $has_hwloc	HAVE_HWLOC
       /proc/cpuinfo      $has_cpuinfo	HAVE_PROC_CPUINFO
])
if test "$enable_cuda" = "yes" -o "$enable_hip" = "yes" -o "$enable_sycl" = "yes" ; then
   AC_MSG_NOTICE([Summary of accellerator support:])
   if test "$enable_cuda" = "yes" ; then
       AC_MSG_NOTICE([    CUDA:
        NVCC		  $NVCC
        NVCCCFLAGS	$NVCCFLAGS
	CUDALD		$CUDALD])
   fi
   if test "$enable_hip" = "yes" ; then
       AC_MSG_NOTICE([    HIP:
        HIPCC		  $HIPCC
        HIPCCFLAGS	$HIPCCFLAGS])
   fi
fi

# Output makefiles
AC_CONFIG_FILES([Makefile
	libsrc/Makefile
	libsrc/util/Makefile
	libsrc/util/tests/Makefile
	libsrc/hwdesc/Makefile
	libsrc/hwdesc/tests/Makefile
	libsrc/cart/Makefile
	libsrc/cart/tests/Makefile
	libsrc/topometrics/Makefile
	libsrc/timearray/Makefile
	src/Makefile
	src/dttest/Makefile
	src/hwinfo/Makefile
	src/mpivars/Makefile
	src/pingpong/Makefile
	src/smptest/Makefile
	src/stress/Makefile
	src/mpiexamples/Makefile
	src/mpiexamples/stencil/Makefile
	src/iotest/Makefile
	doc/Makefile
])
AC_OUTPUT
