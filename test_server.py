#!/usr/bin/python
import optparse
import subprocess

def exe(cmdline):
  print cmdline
  p = subprocess.Popen(cmdline.split(), stdout=subprocess.PIPE)
  out, err = p.communicate()
  return out

def write_rdma():
  print exe("./server3 -e 32 -w")

def main():
  parser = optparse.OptionParser()
  parser.add_option('--write-rdma', action='store_true', default=False, dest='writerdma')

  (options, args) = parser.parse_args()

  if options.writerdma:
    write_rdma()

if __name__ == "__main__":
  main()
