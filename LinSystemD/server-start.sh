#!/bin/bash
LOCAL_IP=$(ifconfig | grep -m 1 -A 6 ^eth | grep -Po "(?<=inet )[0-9]*.[0-9]*.[0-9]*.[0-9]*")
echo starting soe on inet $LOCAL_IP
/opt/soe/soe -addr $LOCAL_IP
