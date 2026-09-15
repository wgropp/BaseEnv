#ifndef ARRINDEX_H_INCLUDED
#define ARRINDEX_H_INCLUDED 1

/* Macros to simplify working with multidimensional arrays */
/* Add for specific numbers of dimensions as needed */
/* idx3l is a version of idx3 that uses a long rather than int, even if
   the individual elements are ints. This is needed for some very large
   3d arrays */
#define idx3l(_i0,_i1,_i2,_n0,_n1,_n2) (((_i0) * (long)(_n1) + _i1)*(_n2)+_i2)
#define idx3(_i0,_i1,_i2,_n0,_n1,_n2) (((_i0) * (_n1) + _i1)*(_n2)+_i2)
#define idx2(_i0,_i1,_n0,_n1) (((_i0) * (_n1) + _i1))

#endif
