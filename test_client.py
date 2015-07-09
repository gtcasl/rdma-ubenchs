#!/usr/bin/python
import optparse
import sys
import time
from common import *

def server_sends(option, opt, value, parser):
  # needed keys don't alter the result of the benchmark because we don't
  # have to send those to the client. only the output keys have an impact
  # on the benchmark
  for output_keys in ENTRIES:
    time.sleep(1)
    out = exe("taskset -c 11 ./client3 -s -n 1024 -o {0} -m {1}".format(output_keys, value))
    print "num output keys = {0}".format(output_keys)
    print_stats(value, out)

  sys.exit(0)

def server_writes(option, opt, value, parser):
  # needed keys don't alter the result of the benchmark because we don't
  # have to send those to the client. only the output keys have an impact
  # on the benchmark
  for output_keys in ENTRIES:
    time.sleep(1)
    out = exe("taskset -c 11 ./client3 -w -n 1024 -o {0} -m {1}".format(output_keys, value))
    print "num output keys = {0}".format(output_keys)
    print_stats(value, out)

  sys.exit(0)

def client_computes(option, opt, value, parser):
  # output keys don't alter the result of the benchmark because we don't
  # have to send those to the client. only the needed keys have an impact
  # on the benchmark
  for needed_keys in ENTRIES:
    time.sleep(1)
    cmd = "taskset -c 11 ./client3 -n {0} -o 1024 -m {1}".format(
                                      needed_keys, value)
    out = exe(cmd)
    print "num needed keys = {0}".format(needed_keys)
    print_stats(value, out)

  sys.exit(0)

def client_reads(option, opt, value, parser):
  for needed_keys in ENTRIES:
    time.sleep(1)
    cmd = "taskset -c 11 ./client3 -r -n {0} -o 1024 -m {1}".format(
                                      needed_keys, value)
    out = exe(cmd)
    print "num needed keys = {0}".format(needed_keys)
    print_stats(value, out)

  sys.exit(0)

def main():
  parser = optparse.OptionParser()
  parser.add_option('--server-sends', action='callback', callback=server_sends,
                    dest='measure', type='str')
  parser.add_option('--server-writes', action='callback', callback=server_writes,
                    dest='measure', type='str')
  parser.add_option('--client-computes', action='callback', callback=client_computes,
                    dest='measure', type='str')
  parser.add_option('--client-reads', action='callback', callback=client_reads,
                    dest='measure', type='str')

  (options, args) = parser.parse_args()

  parser.error("need to select at least one option")

if __name__ == "__main__":
  main()
