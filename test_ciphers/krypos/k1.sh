#!/bin/bash

./quagmire3 -cipher k1.txt -ngramsize 4 -ngramfile english_quadgrams.txt -nsigmathreshold 1. -nlocal 1 -nhillclimbs 500 -nrestarts 100000 -backtrackprob 0.15 -verbose


