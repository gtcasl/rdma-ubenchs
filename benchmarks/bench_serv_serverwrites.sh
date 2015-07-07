#!/bin/bash

OUTPUT=out_srv_serverwrites
OPTION=--server-writes

echo "starting time benchmark"
./test_server.py $OPTION time > $OUTPUT
echo "starting cycles benchmark"
./test_server.py $OPTION cycles >> $OUTPUT
echo "starting cachemisses benchmark"
./test_server.py $OPTION cachemisses >> $OUTPUT
