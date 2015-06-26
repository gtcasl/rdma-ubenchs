import subprocess
import re

NUM_REPETITION = 20
ENTRIES = [4, 16, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304]
PERF_EVENTS = ("cache-references,cache-misses,task-clock,context-switches,"
              "cpu-migrations,page-faults,cycles,instructions,branch-misses,branches")

def exe(cmdline):
  print "executing: " + cmdline
  p = subprocess.Popen(cmdline.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  out, err = p.communicate()
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

  return sum / len(nums)

def get_elapsed(buf):
  res = []

  for line in buf.split("\n"):
    if "elapsed time" in line:
      for token in line.split():
        if is_int(token):
          res.append(int(token))

  return res

def perf_avg(buf, needle):
  regex = '^([0-9]+),{0}'.format(needle)
  res = re.search(regex, buf, re.MULTILINE)
  return res.group(1)

def perf_stdev(buf, needle):
  regex = '^[0-9]+,{0},([0-9]+\.[0-9]+)'.format(needle)
  res = re.search(regex, buf, re.MULTILINE)
  return res.group(1)
