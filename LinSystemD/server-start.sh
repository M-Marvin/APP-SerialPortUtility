#!/bin/bash
while [ -z "$LOCAL_IP" ]
do
	LOCAL_IP=$(ifconfig | grep -m 1 -A 6 ^eth0 | grep -m 1 -Po "(?<=inet6\s)(fd4f(?::\w{1,4}){1,7})(?=\s)")
	#LOCAL_IP=$(ifconfig | grep -m 1 -A 6 ^eth | grep -Po "(?<=inet\s)((?:\w{1,3}.){3}\w{1,3})(?=\s)")
	sleep 1
done
echo starting soe on inet $LOCAL_IP
/opt/soe/soe -addr $LOCAL_IP
