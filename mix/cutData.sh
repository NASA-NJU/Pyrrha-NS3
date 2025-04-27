# @Author: wqy
# @Date:   2020-12-28 11:30:43
# @Last Modified by:   wqy
# @Last Modified time: 2020-12-28 11:30:57
if [[ $1 == "-h" ]]; then
	echo "usage: $0 [path] [outputPath] [file pattern] [grep pattern] [cutline]"
else
	path=$1
	outputPath=$2
	appendix=$3
	grepPattern=$4
	cutline=$5

	for file in `ls "$path" | grep "$appendix" | grep "log"`; do
        	filename=${file%.*}
        	echo "to cut data: $path/$file > $outputPath/$filename.data"
		grep -A$cutline "$grepPattern" $path/$file | tail -$cutline > $outputPath/$filename.data
	done
fi
