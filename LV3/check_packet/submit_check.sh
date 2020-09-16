#!/bin/sh
for((i=1;i<8301;i++))
do
  for((j=15;j<17;j++))
  do    
    ./check $i $j $1 $2

  done
done
