#!/usr/bin/python
import optparse
import sys
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
    print "cache-references={0}".format(get_perf_result(out, "cache-references"))
    print "cache-misses={0}".format(get_perf_result(out, "cache-misses"))
    print "cycles={0}".format(get_perf_result(out, "cycles"))
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
    print "cache-references={0}".format(get_perf_result(out, "cache-references"))
    print "cache-misses={0}".format(get_perf_result(out, "cache-misses"))
    print "cycles={0}".format(get_perf_result(out, "cycles"))
    print "\n"

  sys.exit(0)

def main():
  parser = optparse.OptionParser()
  parser.add_option('--server-sends', action='callback', callback=server_sends)
  parser.add_option('--server-writes', action='callback', callback=server_writes)

  (options, args) = parser.parse_args()

  parser.error("need to select at least one option")

if __name__ == "__main__":
  main()
