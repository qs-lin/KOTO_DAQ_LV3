#!/bin/sh
for((i=1;i<8001;i++))
do
  for((j=15;j<17;j++))
  do    
    ./check $i $j $1

  done
done
