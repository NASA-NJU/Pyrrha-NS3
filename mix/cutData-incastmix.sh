# @Author: wqy
# @Date:   2021-01-21 11:45:33
# @Last Modified by:   wqy
# @Last Modified time: 2021-01-21 14:23:17
if [[ $1 == "-h" ]]; then
	echo "usage: $0 [path] [outputPath] [file pattern] [#incast] [#poisson]"
else
	path=$1
	outputPath=$2
	appendix=$3
	incast_num=$4
	poisson_num=$5

	for file in `ls "$path" | grep "$appendix" | grep ".data" | grep -v "\-poisson.data" | grep -v "\-incast.data"`; do
		# handle data file
    	filename=${file%.*}
    	echo "to cut data: $path/$file > $outputPath/$filename-poisson.data"
    	echo "to cut data: $path/$file > $outputPath/$filename-incast.data"
		tail -$poisson_num $path/$file > $outputPath/$filename-poisson.data
		head -$incast_num $path/$file > $outputPath/$filename-incast.data
	done
fi
