#!/bin/sh
for((i=1;i<1001;i++))
do
  for((j=15;j<17;j++))
  do    
    ./check_vary $i $j 

  done
done
