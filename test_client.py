#!/usr/bin/python
import optparse
import sys
from common import *

def write_rdma(option, opt, value, parser):
  entries = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768]

  for entry in entries:
    for exp in range(NUM_REPETITION):
      exe("./client3 -e {0} -w".format(entry))

  sys.exit(0)

def main():
  parser = optparse.OptionParser()
  parser.add_option('--write-rdma', action='callback', callback=write_rdma)

  (options, args) = parser.parse_args()

  parser.error("need to select at least one option")

if __name__ == "__main__":
  main()
