# @Author: wqy
# @Date:   2021-11-26 15:17:00
# @Last Modified by:   wqy
# @Last Modified time: 2021-11-26 15:18:18

if [[ $1 == "-h" ]]; then
    echo "usage: $0 [log_path] [baseFile] [param] [type:bool|int|double] [origin_value] [start] [step] [end]"
fi

log_path=$1
baseFile=$2
param=$3
types=$4
origin_value=$5
start=$6
step=$7
end=$8

values=()

if [[ $types == "bool" ]]; then
    values[0]="false"
    values[1]="true"
elif [[ $types == "int" ]]; then
    index=0
    for (( i = $start; i < $end+1; i=i+$step )); do
        values[$index]="$i"
        index=$index+1
    done
elif [[ $types == "double" ]];then
    index=0
    curr=$start
    judge=$(echo "$curr<=$end" | bc)
    while [[ $judge -ne 0 ]];do
        values[$index]="$curr"
        index=$index+1
        curr=$(printf %0.1f `echo "scale = 1; $curr+$step" |bc`)
        judge=$(echo "$curr<=$end" | bc)
    done
fi

baseFile=${baseFile%.*}
echo $baseFile
for value in ${values[@]}; do
    curr_file=`echo $baseFile | sed "s/$param-$origin_value/$param-$value/"`
    echo "handle file: $curr_file"
    suffix=`grep "REALTIME_BUFFER_BW_FILE" $log_path/$curr_file.ini | awk -F'/' '{print $NF}'`
    realtime_path=`grep "REALTIME_BUFFER_BW_FILE" $log_path/$curr_file.ini | awk -F'/' '{print $2}'`
    key=`grep "FLOW_CDF" $log_path/$curr_file.ini | awk -F" " '{print $NF}'`
    poisson=`grep "FLOW_NUM" $log_path/$curr_file.ini | awk -F" " '{print $NF}'`
    incast_one_time=`grep "INCAST_MIX" $log_path/$curr_file.ini | awk -F" " '{print $NF}'`
    incast_num=`grep "incast_time" $log_path/$curr_file.log | awk -F" " '{print $2}'`
    incast_num=`expr $incast_num \* $incast_one_time`
    echo -e "suffix: $suffix\tFLOW_CDF: $key\tpoisson_num: $poisson\tincast_one_time: $incast_one_time\tincast_num: $incast_num"
    # statistic in log
    grep -A6 "^mct" $log_path/$curr_file.log
    grep "bw:" $log_path/$curr_file.log
    grep "Length" $log_path/$curr_file.log
    grep "VOQNum" $log_path/$curr_file.log
    grep "PortVOQ" $log_path/$curr_file.log
    # poisson and incast FCT
    ./calcuIncastmix_2.sh $log_path $curr_file.data 10 $incast_num
    # buffer info from realtime buffer output
    ./getBuffer.sh $realtime_path $suffix 10 4
    # queuing time at each hop
    ./getQueuingTime.sh $realtime_path/$suffix-dataQueuing.log $poisson
    # pfc time
    echo $realtime_path/$suffix
    python3 handle_pfc_time_2.py $realtime_path/$suffix 160 170
done