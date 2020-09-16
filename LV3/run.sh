#!/bin/sh
/root/LV3_code/newLV3/ToyDAQ_v6/collect &
COL_PID="$!"
echo "collect PID is $COL_PID"
#kill -2 ${COL_PID}
