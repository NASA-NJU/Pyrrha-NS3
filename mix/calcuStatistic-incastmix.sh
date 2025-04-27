# @Author: wqy
# @Date:   2021-01-21 14:12:22
# @Last Modified by:   wqy
# @Last Modified time: 2021-01-21 14:55:21

echoPercent(){
	lines=$1
	p=$2
	p_line=`echo "scale = 0; $lines * $p / 100" | bc`
	p_data=`cut -f $calcu_line $path/$file | sort -n | head -$p_line | tail -1`
	echo "$p_data"
}

if [[ $1 == "-h" ]]; then
	echo "usage: $0 [path] [file pattern] [calcu_line]"
else
	path=$1
	appendix=$2
	calcu_line=$3

	for file in `ls "$path" | grep "$appendix" | grep ".data"`; do
    	filename=${file%.*}
    	echo "$path/$file: "

		avg=`awk '{s += $'$calcu_line'} END {print s/NR}' "$path/$file"`
		lines=`cut -f 1 $path/$file | wc -l`
		echo "avg 25th 50th 90th 95th 99th"
		echo $avg
		echoPercent $lines 25
		echoPercent $lines 50
		echoPercent $lines 90
		echoPercent $lines 95
		echoPercent $lines 99
		echoPercent $lines 100
	done
fi
