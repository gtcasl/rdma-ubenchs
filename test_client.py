#!/usr/bin/python
import optparse
import sys
import time
from common import *

def server_sends(option, opt, measure, parser):
  experiment("client3", "-s", parser.values.function, measure)
  sys.exit(0)

def server_writes(option, opt, measure, parser):
  experiment("client3", "-w", parser.values.function, measure)
  sys.exit(0)

def client_reads(option, opt, measure, parser):
  experiment("client3", "-r", parser.values.function, measure)
  sys.exit(0)

def main():
  parser = optparse.OptionParser()
  parser.add_option("-f", "--function", dest="function")
  parser.add_option('--server-sends', action='callback', callback=server_sends,
                    dest='measure', type='str')
  parser.add_option('--server-writes', action='callback', callback=server_writes,
                    dest='measure', type='str')
  parser.add_option('--client-reads', action='callback', callback=client_reads,
                    dest='measure', type='str')

  (options, args) = parser.parse_args()

  parser.error("need to select at least one option")

if __name__ == "__main__":
  main()
