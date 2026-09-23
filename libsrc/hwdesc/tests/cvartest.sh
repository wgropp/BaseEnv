#! /bin/sh
./cvartest -nsocket 4 -nnuma 2 -ppn 128 >cvartest.out
cmp cvartest.out cvartest.eo
exit $?
