# @Author: wqy
# @Date:   2021-03-09 17:28:42
# @Last Modified by:   wqy
# @Last Modified time: 2022-01-07 11:36:10

csv_out=0
csv_file="tmp.csv"

if [[ $1 == "-h" ]]; then
	echo "usage: $0 [filename] [poisson_num]"
else
	filename=$1
	poisson_num=$2

	if [[ $# -eq 3 ]]; then
		csv_out=1
		csv_file=$3
	elif [[ $# -lt 2 ]]; then
		echo "usage: $0 [filename] [poisson_num]"
	fi

	curr=`awk 'BEGIN {
		sum_poisson_src = 0
		sum_poisson_core = 0
		sum_poisson_dst = 0
		num_poisson = 0
		sum_incast_src = 0
		sum_incast_core = 0
		sum_incast_dst = 0
		num_incast = 0
	} {
		if ($1 < '$poisson_num'){
			sum_poisson_src += $8
			sum_poisson_core += $9
			sum_poisson_dst += $10
			num_poisson += 1
		}else{
			sum_incast_src += $8
			sum_incast_core += $9
			sum_incast_dst += $10
			num_incast += 1
		}
	} END{
		printf num_poisson; printf " "
		if (num_poisson > 0){
			printf sum_poisson_src/num_poisson/1e3; printf " "
			printf sum_poisson_core/num_poisson/1e3; printf " "
			printf sum_poisson_dst/num_poisson/1e3; printf " "
			printf (sum_poisson_dst+sum_poisson_core+sum_poisson_src)/num_poisson/1e3; printf " "
		}
		printf num_incast; printf " "
		if (num_incast > 0){
			printf sum_incast_src/num_incast/1e3; printf " "
			printf sum_incast_core/num_incast/1e3; printf " "
			printf sum_incast_dst/num_incast/1e3; printf " "
			printf (sum_incast_dst+sum_incast_src+sum_incast_core)/num_incast/1e3
		}
	}' $filename`

	OLD_IFS="$IFS"
	IFS=" "
	curr_arr=($curr)
	IFS="$OLD_IFS"

	i=0
	poisson_num=${curr_arr[$i]}
	echo "poisson_num: $poisson_num"
	if [[ $poisson_num > 0 ]]; then
		poisson_stor=${curr_arr[$i+1]}
		poisson_core=${curr_arr[$i+2]}
		poisson_dtor=${curr_arr[$i+3]}
		poisson_all=${curr_arr[$i+4]}
		i=`expr $i + 5`
		echo "poisson: srcToR core dstToR all"
		echo $poisson_stor
		echo $poisson_core
		echo $poisson_dtor
		echo $poisson_all
		if [[ $csv_out -eq 1 ]]; then
			echo "$poisson_stor," >> $csv_file
			echo "$poisson_core," >> $csv_file
			echo "$poisson_dtor," >> $csv_file
			echo "$poisson_all," >> $csv_file
		fi
	else
		if [[ $csv_out -eq 1 ]]; then
			echo "0," >> $csv_file
			echo "0," >> $csv_file
			echo "0," >> $csv_file
			echo "0," >> $csv_file
		fi
	fi

	incast_num=${curr_arr[$i]}
	echo "incast_num: $incast_num"
	if [[ $incast_num > 0 ]]; then
		incast_stor=${curr_arr[$i+1]}
		incast_core=${curr_arr[$i+2]}
		incast_dtor=${curr_arr[$i+3]}
		incast_all=${curr_arr[$i+4]}
		echo "incast: srcToR core dstToR all"
		echo $incast_stor
		echo $incast_core
		echo $incast_dtor
		echo $incast_all
		if [[ $csv_out -eq 1 ]]; then
			echo "$incast_stor," >> $csv_file
			echo "$incast_core," >> $csv_file
			echo "$incast_dtor," >> $csv_file
			echo "$incast_all," >> $csv_file
		fi
	else
		if [[ $csv_out -eq 1 ]]; then
			echo "0," >> $csv_file
			echo "0," >> $csv_file
			echo "0," >> $csv_file
			echo "0," >> $csv_file
		fi
	fi
	
fi