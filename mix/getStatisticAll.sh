
incasts=(["1"]=1 ["2"]=4 ["3"]=18 ["4"]=1 ["5"]=1 ["6"]=1 ["7"]=1 ["8"]=6 ["13"]=1 ["14"]=3 )

if [[ $1 == "-h" ]];then
	echo "usage: $0 [log path] [realtime output path] [log suffix] [#flows of one incast]"
	echo -e "attention: \n1. Make sure the required scripts have exsiten at the same path with this script file: calcuIncastmix_2.sh, getBuffer.sh, getQueuingTime.sh"
	echo "2. Run cutData_2.sh before this command to get .data files, and .data files should be at the same path with .log file."
	echo "3. Check the incast times in this script."
else
	log_path=$1
	realtime_path=$2
	suffix=$3
	incast_one_time=$4

	for key in $(echo ${!incasts[*]})
	do
		echo "$log_path/config-$suffix-FLOW_CDF-$key.log"
		poisson=50000
        	if [[ $key == 2 || $key == 3 || $key == 8 ]];then
                	poisson=20000
        	fi
		# statistic in log
		tail -52 $log_path/config-$suffix-FLOW_CDF-$key.log
		# poisson and incast FCT
		incast_num=`expr ${incasts[$key]} \* $incast_one_time`
		./calcuIncastmix_2.sh $log_path config-$suffix-FLOW_CDF-$key.data 10 $incast_num  
		# buffer info from realtime buffer output
		./getBuffer.sh $realtime_path $suffix-drill-flowcdf$key 10 4
		# queuing time at each hop
		./getQueuingTime.sh $realtime_path/$suffix-drill-flowcdf$key-dataQueuing.log $poisson
	done
fi
