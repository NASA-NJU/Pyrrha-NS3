# @Author: wqy
# @Date:   2021-03-04 11:42:13
# @Last Modified by:   wqy
# @Last Modified time: 2021-11-30 15:39:45

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

echoSlowdown(){
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

printSlowdown(){
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
	echoSlowdown $filename $line_num 25
	echoSlowdown $filename $line_num 50
	echoSlowdown $filename $line_num 90
	echoSlowdown $filename $line_num 95
	echoSlowdown $filename $line_num 99
}

if [[ $1 == "-h" ]]; then
	echo "usage: $0 [path] [file_suffix] [calcu_line] [slowdown_line] [tail_num] [csv file]"
else
	path=$1
	suffix=$2
	calcu_line=$3
	slowdown_line=$4
	incast_num=$5

	if [[ $# -eq 6 ]]; then
		csv_out=1
		csv_file=$6
	elif [[ $# -lt 5 ]]; then
		echo "usage: $0 [path] [file_suffix] [calcu_line] [slowdown_line] [tail_num] [csv file(if need)]"
	fi

	for file in `ls "$path" | grep "$suffix" | grep ".data$"`
	do
	    # echo "$key : ${dic[$key]}"
		echo $file
	    filename=${file%.*}
		filename_poisson="$path/$filename-poisson.data"
		filename_incast="$path/$filename-incast.data"
		all_num=`wc -l $path/$file | awk '{print $1}'`
		poisson_num=`expr $all_num - $incast_num`

		echo "output $poisson_num: $filename_poisson"
		head -$poisson_num $path/$file > $filename_poisson

		echo "output $incast_num: $filename_incast"
		tail -$incast_num $path/$file > $filename_incast

		# poisson
		printPercentile $filename_poisson $calcu_line "poisson"
		# incast 
		printPercentile $filename_incast $calcu_line "incast"

		# poisson-slowdown
		printSlowdown $filename_poisson $slowdown_line "poisson-slowdown"
		# incast-slowdown 
		printSlowdown $filename_incast $slowdown_line "incast-slowdown"

		# rm -rf $filename_poisson
		# rm -rf $filename_incast
	done

fi
