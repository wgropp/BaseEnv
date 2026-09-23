#! /bin/bash
./factortst >factortst.out 2>&1
cmp factortst.out factortst.eo
