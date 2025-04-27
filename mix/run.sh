if [[ $1 == "-h" ]];then
	echo "usage: $0 [config pattern] [queue num]"
else
	if [[ $# < 1 || $# > 2 ]]; then
		exit 1
	fi

	appendix=$1

	path=`pwd`
	dirname=${path##*/}
	echo "currdir:$dirname"
	cd ..

	queue_num=8
	if [[ $# == 2 ]]; then
		queue_num=$2
	fi
	echo "queue_num: $queue_num"
	old=`grep "static const uint32_t QUEUENUM" src/point-to-point/model/settings.h`
	sed -i "s/$old/static const uint32_t QUEUENUM = $queue_num;/" src/point-to-point/model/settings.h

	./waf build
	for file in `ls "$dirname" | grep "$appendix" | grep "ini"`; do
		filename=${file%.*}
		echo "to run: $dirname/$file > $dirname/$filename.log"
		./waf --run "third $dirname/$file" &> $dirname/$filename.log &
	done
fi
