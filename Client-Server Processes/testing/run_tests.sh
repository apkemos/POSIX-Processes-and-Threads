#!/bin/bash
#make it executable with chmod +x run_tests.sh
#or chmod 777 run_tests.sh
#Run with bash ./run_tests.sh
#Must be killed after manually
COUNTER=0
LIMIT=2
name[0]='Player1'
name[1]='Player2'



inventory[0]='testing/inv1.txt'
inventory[1]='testing/inv2.txt'


cd ..; #Go to project's directory
 ./gameserver -p 2 -i testing/inventory.txt -q 1000 &

while [ $COUNTER -lt $LIMIT ];
do
sleep 3
./player -n ${name[COUNTER]} -i ${inventory[COUNTER]} localhost &
let COUNTER+=1
done

