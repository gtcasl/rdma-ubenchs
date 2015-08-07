import subprocess
import re
import math
import time

ENTRIES = [1024, 16384, 262144, 4194304]
#ENTRIES = [4, 16, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216, 67108864]
COSTS = [1, 256, 512, 768, 1024]

def exe(cmdline):
  print "executing: " + cmdline
  p = subprocess.Popen(cmdline.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  out, err = p.communicate()
  if p.returncode != 0:
    print out, err
    raise RuntimeError("return code not 0")
  return out + err

def is_int(str):
  try:
    int(str)
    return True
  except ValueError:
    return False

def avg(nums):
  sum = 0
  for num in nums:
    sum = sum + num

  return sum / float(len(nums))

def std_dev(nums, avg):
  sum = 0
  for n in nums:
    sum = sum + math.pow(n - avg, 2)

  sample_var = sum / float((len(nums) - 1))
  return math.sqrt(sample_var)

def extract_nums(buf, label):
  res = []

  for line in buf.split("\n"):
    if label in line:
      for token in line.split():
        if is_int(token):
          res.append(int(token))

  return res

def print_stats(measure, out):
  if measure == "time":
    times = extract_nums(out, "time")
    average = avg(times)
    print "avg time={0}, sample std dev={1}".format(average, std_dev(times, average))
  elif measure == "cycles":
    cycles = extract_nums(out, "cycles")
    average = avg(cycles)
    print "avg cycles={0}, sample std dev={1}".format(average, std_dev(cycles, average))
  elif measure == "cachemisses":
    misses = extract_nums(out, "cachemisses")
    average = avg(misses)
    print "avg misses={0}, sample std dev={1}".format(average, std_dev(misses, average))
  else:
    raise LookupError("Measure not supported")

  print "\n"

def experiment(binary, transfer_opt, func, measure):
  for num_entries in ENTRIES:
    for cost in COSTS:
      if cost >= num_entries * 4:
        break

      if binary == "client3":
        time.sleep(5)
      else:
        time.sleep(1)

      out = exe("taskset -c 11 ./{0} {1} -i {2} -m {3} -c {4} -f {5}".format(binary, transfer_opt,
                                                              num_entries, measure, cost, func))
      print "func = {0}, num entries = {1}, cost = {2}".format(func, num_entries, cost)
      print_stats(measure, out)

