#!/usr/bin/python
import optparse
import sys
from common import *

def write_rdma(option, opt, value, parser):
  for entry in ENTRIES:
    for matching_keys in ENTRIES:
      if matching_keys >= entry:
        break;

      cmd = "perf stat -e {3} -x, -r {2} taskset 0x1011 ./server3 -e {0} -w -k {1}".format(entry,
                                      matching_keys, NUM_REPETITION, PERF_EVENTS)
      out = exe(cmd)
      times = get_elapsed(out)
      print "Num entries={0}, num matching keys={1}, avg={2}".format(entry, matching_keys, avg(times))
      cacherefs = get_perf_result(out, "cache-references")
      print "cache-references={0}".format(cacherefs)

  sys.exit(0)

def main():
  parser = optparse.OptionParser()
  parser.add_option('--write-rdma', action='callback', callback=write_rdma)

  (options, args) = parser.parse_args()

  parser.error("need to select at least one option")

if __name__ == "__main__":
  main()
