import subprocess

NUM_REPETITION = 10

def exe(cmdline):
  print cmdline
  p = subprocess.Popen(cmdline.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  out, err = p.communicate()
  return out

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
  for line in buf.split("\n"):
    if "elapsed time" in line:
      for token in line.split():
        if is_int(token):
          return int(token)

  raise LookupError("could not find elapsed time")
