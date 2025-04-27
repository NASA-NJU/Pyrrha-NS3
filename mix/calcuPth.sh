# @Author: wqy
# @Date:   2021-03-04 11:42:13
# @Last Modified by:   wqy
# @Last Modified time: 2022-01-13 15:53:58

csv_out=0
csv_file="tmp.csv"

echoPercent(){
    file=$1
    lines=$2
    p=$3

    p_line=`echo "scale = 0; $lines * $p / 100" | bc`
    p_data=`cut -f $calcu_line $file | sort -n | head -$p_line | tail -1`
    echo "$p_data"
    if [[ $csv_out -eq 1 ]]; then
        echo "$p_data," >> $csv_file 
    fi
    
}

printPercentile(){
    filename=$1
    calcu_line=$2
    echo_str=$3

    line_num=`wc -l $filename | awk '{print $1}'`
    avg=`awk '{s += $'$calcu_line'} END {print s/NR}' "$filename"`
    echo "$echo_str: avg 25th 50th 90th 95th 99th"
    echo $avg
    if [[ $csv_out -eq 1 ]]; then
        echo "$avg," >> $csv_file
    fi
    echoPercent $filename $line_num 25
    echoPercent $filename $line_num 50
    echoPercent $filename $line_num 90
    echoPercent $filename $line_num 95
    echoPercent $filename $line_num 99
}

if [[ $1 == "-h" ]]; then
    echo "usage: $0 [path] [file_suffix] [calcu_line] [csv file]"
else
    path=$1
    suffix=$2
    calcu_line=$3

    if [[ $# -eq 4 ]]; then
        csv_out=1
        csv_file=$4
    elif [[ $# -lt 3 ]]; then
        echo "usage: $0 [path] [file_suffix] [calcu_line] [csv file(if need)]"
    fi

    for file in `ls "$path" | grep "$suffix" | grep ".data$"`
    do
        # echo "$key : ${dic[$key]}"
        echo $file
        printPercentile $path/$file $calcu_line "pth:"
    done

fi
