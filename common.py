import subprocess
import re

NUM_REPETITION = 10
ENTRIES = [2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768]
PERF_EVENTS = ("cache-references,cache-misses,task-clock,context-switches,"
              "cpu-migrations,page-faults,cycles,instructions,branch-misses,branches")

def exe(cmdline):
  print cmdline
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

def get_perf_result(buf, needle):
  regex = '^([0-9]+),{0}'.format(needle)
  res = re.search(regex, buf, re.MULTILINE)
  return res.group(1)
