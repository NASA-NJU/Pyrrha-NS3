cd ..
./waf build
suffix=$1
for ((i=1;i<=9;i++))do
{
	echo "mix/config-${suffix}-${i}0ToR.ini > mix/config-${suffix}-${i}0ToR.log"
	./waf --run "third mix/config-${suffix}-${i}0ToR.ini" &> mix/config-${suffix}-${i}0ToR.log &
}
done
