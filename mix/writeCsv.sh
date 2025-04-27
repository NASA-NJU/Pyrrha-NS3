# @Author: wqy
# @Date:   2021-11-29 16:45:35
# @Last Modified by:   wqy
# @Last Modified time: 2021-11-30 15:41:12

if [[ $1 == "-h" ]]; then
    echo "usage: $0 [log_path] [baseFile] [param] [type:bool|int|double] [origin_value] [start] [step] [end] [out_file]"
fi

log_path=$1
baseFile=$2
param=$3
types=$4
origin_value=$5
start=$6
step=$7
end=$8
out_file=" "
csv_out=0

if [[ $# -eq 9 ]]; then
    csv_out=1
    out_file=$9
elif [[ $# -lt 8 ]]; then
    echo "usage: $0 [log_path] [baseFile] [param] [type:bool|int|double] [origin_value] [start] [step] [end] [out_file]"
fi

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
if [[ $csv_out -eq 1 ]]; then
    echo $baseFile >> $out_file
fi
for value in ${values[@]}; do
    curr_file=`echo $baseFile | sed "s/$param-$origin_value/$param-$value/"`
    echo $curr_file

    # get config info
    suffix=`grep "REALTIME_BUFFER_BW_FILE" $log_path/$curr_file.ini | awk -F'/' '{print $NF}'`
    realtime_path=`grep "REALTIME_BUFFER_BW_FILE" $log_path/$curr_file.ini | awk -F'/' '{print $2}'`
    key=`grep "FLOW_CDF" $log_path/$curr_file.ini | awk -F" " '{print $NF}'`
    poisson=`grep "FLOW_NUM" $log_path/$curr_file.ini | awk -F" " '{print $NF}'`
    incast_one_time=`grep "INCAST_MIX" $log_path/$curr_file.ini | awk -F" " '{print $NF}'`
    incast_num=`grep "incast_time" $log_path/$curr_file.log | awk -F" " '{print $2}'`
    incast_num=`expr $incast_num \* $incast_one_time`
    fc_mode=`grep "FC_MODE " $log_path/$curr_file.ini | awk '{print $2}'`
    echo -e "suffix: $suffix\tFLOW_CDF: $key\tpoisson_num: $poisson\tincast_one_time: $incast_one_time\tincast_num: $incast_num"

    # rcv fct
    grep -A6 "^mct(rcv)" $log_path/$curr_file.log
    if [[ $csv_out -eq 1 ]]; then
        grep -A6 "^mct(rcv)" $log_path/$curr_file.log | tail -6 | awk '{print $0","}' >> $out_file
    fi
    # src fct
    grep -A6 "^mct(src)" $log_path/$curr_file.log
    if [[ $csv_out -eq 1 ]]; then
        grep -A6 "^mct(src)" $log_path/$curr_file.log | tail -6 | awk '{print $0","}' >> $out_file
    fi

    # poisson and incast FCT
    ./calcuIncastmix_2.sh $log_path $curr_file.data 10 $incast_num $out_file

    # avg bw
    grep "bw:" $log_path/$curr_file.log
    if [[ $csv_out -eq 1 ]]; then
        grep "bw:" $log_path/$curr_file.log | awk '{print $2","}' >> $out_file
    fi

    # max buffer from log file
    grep "Length" $log_path/$curr_file.log
    grep "VOQNum" $log_path/$curr_file.log
    if [[ $csv_out -eq 1 ]]; then
        grep "maxPortLength:" $log_path/$curr_file.log | awk '{print $2","}' >> $out_file
        grep "maxSwitchLength:" $log_path/$curr_file.log | awk '{print $2","}' >> $out_file
        if [[ $fc_mode -eq 1 ]]; then
            grep "maxUsedVOQNum:" $log_path/$curr_file.log | awk '{print $2","}' >> $out_file
            grep "maxVOQLength:" $log_path/$curr_file.log | awk '{print $4","}' >> $out_file
        elif [[ $fc_mode -eq 0 ]]; then
            echo "," >> $out_file
            echo "," >> $out_file
        elif [[ $fc_mode -eq 3 ]]; then
            active=`grep "maxActivePortVOQ:" $log_path/$curr_file.log | awk '{print $2}'`
            all=`grep "maxPortVOQ:" $log_path/$curr_file.log | awk '{print $2}'`
            echo "$active.$all," >> $out_file
            grep "maxPortVOQLen:" $log_path/$curr_file.log | awk '{print $2","}' >> $out_file
        fi
    fi
    

    # buffer info from realtime buffer output
    ./getBuffer.sh $realtime_path $suffix 10 4 $out_file

    # queuing time at each hop
    ./getQueuingTime.sh $realtime_path/$suffix-dataQueuing.log $poisson $out_file

    # pfc time
    # python3 handle_pfc_time_2.py $realtime_path/$suffix 160 170

done
