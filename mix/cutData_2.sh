# @Author: wqy
# @Date:   2021-02-28 17:42:58
# @Last Modified by:   wqy
# @Last Modified time: 2022-01-13 15:48:59
if [[ $1 == "-h" ]]; then
	echo "usage: $0 [path] [outputPath] [file pattern] [grep pattern]"
else
	path=$1
	outputPath=$2
	appendix=$3
	grepPattern=$4

	for file in `ls "$path" | grep "$appendix" | grep "log"`; do
	    	filename=${file%.*}
		echo "to cut data: $path/$file > $outputPath/$filename.data"			
		cutline=`grep "^msg finished" $path/$file | tail -1 | awk '{print $3}'`
		echo $cutline
		grep -A$cutline "$grepPattern" $path/$file | tail -$cutline > $outputPath/$filename.data
		poisson_num=`grep "^FLOW_NUM" $path/$file | head -1 | awk '{print $2}'`
	        echo $poisson_num
	        if [[ $poisson_num -ne $cutline ]];then
	                awk '{if($5 < '$poisson_num') {print $0}}' $outputPath/$filename.data > $outputPath/$filename-poisson.data
	                awk '{if($5 >= '$poisson_num') {print $0}}' $outputPath/$filename.data > $outputPath/$filename-incast.data
	                # awk '{if ($2 < 16) {print $0}}' $outputPath/$filename-poisson.data > $outputPath/$filename-poisson-victim.data
	        fi
	done
fi
