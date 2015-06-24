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
    for exp in range(NUM_REPETITION):
      time.sleep(0.25)
      exe("./client3 -n 1024 -n {0} -o 1024".format(needed_keys))

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
