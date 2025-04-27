# @Author: wqy
# @Date:   2020-12-29 16:55:54
# @Last Modified by:   wqy
# @Last Modified time: 2021-11-30 15:40:09

if [[ $1 == "-h" ]]; then
	echo "usage: $0 [baseFile] [param] [type:bool|int|double] [origin_value] [start] [step] [end]"
else
	baseFile=$1
	param=$2
	types=$3
	origin_value=$4
	start=$5
	step=$6
	end=$7

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

	baseFileName=${baseFile%.*}

	for value in ${values[@]}; do
		echo "output: $baseFileName-$param-$value.ini"
		sed "s/$param $origin_value/$param $value/" $baseFile > "$baseFileName-$param-$value.ini"
		if [[ $param == "FLOW_CDF" ]];then
			sed -i "s/flowcdf$origin_value/flowcdf$value/" "$baseFileName-$param-$value.ini"
			if [[ $value == 8 ]];then
				sed -i "s/FLOW_NUM 50000/FLOW_NUM 20000/" "$baseFileName-$param-$value.ini"
			fi
		elif [[ $param == "LOAD" ]];then
			sed -i "s/load$origin_value/load$value/" "$baseFileName-$param-$value.ini"
		elif [[ $param == "SWITCH_WIN_M" ]]; then
			sed -i "s/win$origin_value/win$value/" "$baseFileName-$param-$value.ini"
		fi
	done
fi