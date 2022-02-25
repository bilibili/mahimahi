pids=$(ps -ef |grep nginx | grep master | grep -v 'grep'| awk '{print $2}')
for pid in $pids
do 
  kill $pid
done

pids=$(ps -ef |grep fcgi | grep -v 'grep'|awk '{print $2}')
for pid in $pids
do
  kill $pid
done

