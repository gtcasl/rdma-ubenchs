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
    for exp in range(NUM_REPETITION):
      time.sleep(0.25)
      exe("./client3 -s -n 1024 -o {0}".format(output_keys))

  sys.exit(0)

def server_writes(option, opt, value, parser):
  # needed keys don't alter the result of the benchmark because we don't
  # have to send those to the client. only the output keys have an impact
  # on the benchmark
  for output_keys in ENTRIES:
    for exp in range(NUM_REPETITION):
      time.sleep(0.25)
      exe("./client3 -w -n 1024 -o {0}".format(output_keys))

  sys.exit(0)

def client_computes(option, opt, value, parser):
  # output keys don't alter the result of the benchmark because we don't
  # have to send those to the client. only the needed keys have an impact
  # on the benchmark
  for needed_keys in ENTRIES:
    time.sleep(0.25)
    out = exe("perf stat -e {0} -x, -r {1} taskset -c 11 ./client3 -n {2} -o 1024".format(
                                      PERF_EVENTS, NUM_REPETITION, needed_keys))
    times = get_elapsed(out)
    print "num needed keys={0}, avg={1}".format(needed_keys, avg(times))
    print "cache-references={0}, stdev={1}".format(perf_avg(out, "cache-references"),
                                                  perf_stdev(out, "cache-references"))
    print "cache-misses={0}, stdev={1}".format(perf_avg(out, "cache-misses"),
                                    perf_stdev(out, "cache-misses"))
    print "cycles={0}, stdev={1}".format(perf_avg(out, "cycles"),
                              perf_stdev(out, "cycles"))
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
