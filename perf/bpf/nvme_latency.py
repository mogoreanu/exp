#!/usr/bin/python3

# sudo apt-get install bpfcc-tools linux-headers-$(uname -r)
# python3 nvme_latency.py

from __future__ import print_function
from bcc import BPF
from bcc.utils import printb
from time import sleep

# load BPF program
b = BPF(src_file = "nvme_latency.c")

# if BPF.get_kprobe_functions(b'__blk_account_io_done'):
b.attach_kretprobe(event="nvme_setup_cmd", fn_name="nvme_setup_cmd_return")
b.attach_kprobe(event="nvme_complete_batch_req", fn_name="nvme_complete_rq")
b.attach_kprobe(event="nvme_complete_rq", fn_name="nvme_complete_rq")

# header
print("Tracing... Hit Ctrl-C to end.")

# trace until Ctrl-C
while 1:
  try:
    (task, pid, cpu, flags, ts, msg) = b.trace_fields()
  except ValueError:
    continue
  except KeyboardInterrupt:
    break
  # printb(b"%-18.9f %-16s %-6d %s" % (ts, task, pid, msg))
  print(msg)

print("\nRequest latency histogram")
print("~~~~~~~~~~~~~~~~")
req_lat_hist_us = b["req_lat_hist_us"]
req_lat_hist_us.print_log2_hist("latency_us")

def print_my_log_hist(t):
  first_nonzero = -1
  last_nonzero = 0
  for i in range(0,len(t)):
    if t[i].value > 0:
      last_nonzero = i
      if first_nonzero < 0:
        first_nonzero = i

  for i in range(first_nonzero, last_nonzero + 1):
    if i == 0:
      lower = 0
    else:
      lower = pow(2, i - 1)
    upper = pow(2, i)
    print(i, " [", lower, " ", upper, ") ", t[i].value, sep="")

print_my_log_hist(b["req_lat_hist_us"])

# b["dist"].print_linear_hist("kbytes")
