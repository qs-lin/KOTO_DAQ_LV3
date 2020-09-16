for((i=1;i<20;i++))
do
  dir="/local/s1/toyDAQ_0617/spill${i}"
  if [ ! -d ${dir} ]
  then
    mkdir -p ${dir}
  else
    rm -f ${dir}/*
  fi    

done
