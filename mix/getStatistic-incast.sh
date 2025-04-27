path=$1
suffix=$2
receiver_tor=$3
tor_num=$4
core_num=$5

# get max buffers of ToRs
tor_buffers=()
for ((i=0;i<tor_num;i++));do
	tor_buffers[$i]=`awk 'BEGIN{max = 0} {if ($8 > max) {max = $8}} END{printf max}' $path/${suffix}-buffer${i}.log`
done
# get max buffers of Cores
core_buffers=()
for ((i=0;i<core_num;i++));do
	index=`expr $i + $tor_num`
	core_buffers[$i]=`awk 'BEGIN{max = 0} {if ($8 > max) {max = $8}} END{printf max}' $path/${suffix}-buffer${index}.log`
done

# print buffers 
echo "Receiver buffer: ${tor_buffers[$receiver_tor]}"
echo "ToR buffers: ${tor_buffers[@]}"
echo "Core buffers: ${core_buffers[@]}"

# calculate avg&max
# sender-ToR
avg_tor=0;
max_tor=0;
src_tor=0;
for ((i=0;i<tor_num;i++));do
	if [[ $i -ne $receiver_tor && $max_tor -le ${tor_buffers[$i]} ]];then
		max_tor=${tor_buffers[$i]}
	fi
	if [[ $i -ne $receiver_tor && ${tor_buffers[$i]} -ne 0 ]];then
		avg_tor=`expr $avg_tor + ${tor_buffers[$i]}`
		src_tor=`expr $src_tor + 1`
	fi
done
echo $src_tor
avg_tor=`echo "scale=1;($avg_tor)/($src_tor)" | bc`
echo "Avg max Sender-ToR: $avg_tor"
echo "max sender-ToR: $max_tor"
# core
avg_core=0;
max_core=0;
for ((i=0;i<core_num;i++));do
        if [[ $max_core -lt ${core_buffers[$i]} ]];then
                max_core=${core_buffers[$i]}
        fi
        avg_core=`expr $avg_core + ${core_buffers[$i]}`
done
avg_core=`echo "scale=1;$avg_core/$core_num" | bc`
echo "Avg max Core: $avg_core"
echo "max Core: $max_core"
