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
    cmd = "perf stat -e {0} -x, -r {1} taskset -c 11 ./server3 -s -n 1024 -o {2}".format(
                                            PERF_EVENTS, NUM_REPETITION, output_keys)
    out = exe(cmd)
    times = get_elapsed(out)
    print "num output keys={0}, avg={1}".format(output_keys, avg(times))
    print "cache-references={0}".format(perf_avg(out, "cache-references"))
    print "cache-misses={0}".format(perf_avg(out, "cache-misses"))
    print "cycles={0}".format(perf_avg(out, "cycles"))
    print "\n"

  sys.exit(0)

def server_writes(option, opt, value, parser):
  # needed keys don't alter the result of the benchmark because we don't
  # have to send those to the client. only the output keys have an impact
  # on the benchmark
  for output_keys in ENTRIES:
    cmd = "perf stat -e {0} -x, -r {1} taskset -c 11 ./server3 -w -n 1024 -o {2}".format(
                                            PERF_EVENTS, NUM_REPETITION, output_keys)
    out = exe(cmd)
    times = get_elapsed(out)
    print "num output keys={0}, avg={1}".format(output_keys, avg(times))
    print "cache-references={0}".format(perf_avg(out, "cache-references"))
    print "cache-misses={0}".format(perf_avg(out, "cache-misses"))
    print "cycles={0}".format(perf_avg(out, "cycles"))
    print "\n"

  sys.exit(0)

def client_computes(option, opt, value, parser):
  # needed keys don't alter the result of the benchmark because we don't
  # have to send those to the client. only the output keys have an impact
  # on the benchmark
  for needed_keys in ENTRIES:
    cmd = "perf stat -e {0} -x, -r {1} taskset -c 11 ./server3 -n {2} -o 1024".format(
                                            PERF_EVENTS, NUM_REPETITION, needed_keys)
    out = exe(cmd)
    times = get_elapsed(out)
    print "num needed keys={0}, avg={1}".format(needed_keys, avg(times))
    print "cache-references={0}".format(perf_avg(out, "cache-references"))
    print "cache-misses={0}".format(perf_avg(out, "cache-misses"))
    print "cycles={0}".format(perf_avg(out, "cycles"))
    print "\n"

  sys.exit(0)

def main():
  parser = optparse.OptionParser()
  parser.add_option('--server-sends', action='callback', callback=server_sends)
  parser.add_option('--server-writes', action='callback', callback=server_writes)
  parser.add_option('--client-computes', action='callback', callback=client_computes)

  (options, args) = parser.parse_args()

  parser.error("need to select at least one option")

if __name__ == "__main__":
  main()
