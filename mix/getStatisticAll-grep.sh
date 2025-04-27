# @Author: wqy
# @Date:   2021-11-26 15:17:00
# @Last Modified by:   wqy
# @Last Modified time: 2022-03-30 16:57:37

if [[ $1 == "-h" ]]; then
    echo "usage: $0 [config_path] [log_path] [grep_pattern]"
else
    config_path=$1
    log_path=$2
    grep_pattern=$3
    
    echo "$config_path"
    echo "$log_path"

    for file in `ls "$config_path" | grep "$grep_pattern" | grep ".ini$"`; do
    #for file in `ls "$config_path" | grep ".ini$"`; do
        curr_file=${file%.*}
        echo "handle file: $curr_file"
        suffix=`grep "REALTIME_BUFFER_BW_FILE" $config_path/$curr_file.ini | awk -F'/' '{print $NF}'`
        realtime_path=`grep "REALTIME_BUFFER_BW_FILE" $config_path/$curr_file.ini | awk -F'/' '{print $2}'`
        key=`grep "FLOW_CDF" $config_path/$curr_file.ini | awk -F" " '{print $NF}'`
        poisson=`grep "FLOW_NUM" $config_path/$curr_file.ini | awk -F" " '{print $NF}'`
        incast_one_time=`grep "INCAST_MIX" $config_path/$curr_file.ini | awk -F" " '{print $NF}'`
        if [[ $incast_one_time -eq 0 ]]; then
            # pure poisson
            echo -e "suffix: $suffix\tFLOW_CDF: $key\tpoisson_num: $poisson"
            ./calcuIncastmix_2.sh $log_path $curr_file.data 10 $incast_num
        else
            # incastmix 
            incast_num=`grep "incast_time" $log_path/$curr_file.log | awk -F" " '{print $2}'`
            incast_num=`expr $incast_num \* $incast_one_time`
            echo -e "suffix: $suffix\tFLOW_CDF: $key\tpoisson_num: $poisson\tincast_one_time: $incast_one_time\tincast_num: $incast_num"
            # poisson and incast FCT
            ./calcuIncastmix_2.sh $log_path $curr_file.data 10 $incast_num
        fi
        # statistic in log
        grep -A6 "^mct" $log_path/$curr_file.log
        grep "bw:" $log_path/$curr_file.log
        grep "Length" $log_path/$curr_file.log
        grep "VOQNum" $log_path/$curr_file.log
        grep "PortVOQ" $log_path/$curr_file.log
        grep -A1 "Data packet avg queuing time" $log_path/$curr_file.log
        # buffer info from realtime buffer output
        ./getBuffer.sh $realtime_path $suffix 10 4
        # queuing time at each hop
        ./getQueuingTime.sh $realtime_path/$suffix-dataQueuing.log $poisson
        # pfc time
        echo $realtime_path/$suffix
        python3 handle_pfc_time_2.py $realtime_path/$suffix 160 170
    done
fi