#! /bin/bash
./scheddecode > scheddecode.out
cmp scheddecode.out scheddecode.eo
