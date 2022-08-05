#!/bin/bash

BIN=build/cm2.bin

curl -X PUT --data-binary @${BIN} http://172.16.1.184/ota &
#curl -X PUT --data-binary @build/wifi-blinky.bin http://172.16.1.229/ota &
#curl -X PUT --data-binary @build/wifi-blinky.bin http://172.16.1.217/ota &
#curl -X PUT --data-binary @build/wifi-blinky.bin http://172.16.1.223/ota &
#curl -X PUT --data-binary @build/wifi-blinky.bin http://172.16.1.233/ota &
#curl -X PUT --data-binary @build/wifi-blinky.bin http://172.16.1.184/ota &
wait
echo ""
