#!/bin/bash

OUTPUT=out_clnt_serverwrites
OPTION=--server-writes
echo "starting time benchmark (client)"
./test_client.py $OPTION time > $OUTPUT
echo "starting cycles benchmark (client)"
./test_client.py $OPTION cycles >> $OUTPUT
echo "starting cachemisses benchmark (client)"
./test_client.py $OPTION cachemisses >> $OUTPUT
