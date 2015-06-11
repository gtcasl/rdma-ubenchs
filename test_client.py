#!/usr/bin/python
import optparse
import sys
import time
from common import *

def write_rdma(option, opt, value, parser):
  for entry in ENTRIES:
    for matching_keys in ENTRIES:
      if matching_keys >= entry:
        break;

      for exp in range(NUM_REPETITION):
        time.sleep(0.25)
        exe("./client3 -e {0} -w -k {1}".format(entry, matching_keys))

  sys.exit(0)

def main():
  parser = optparse.OptionParser()
  parser.add_option('--write-rdma', action='callback', callback=write_rdma)

  (options, args) = parser.parse_args()

  parser.error("need to select at least one option")

if __name__ == "__main__":
  main()
