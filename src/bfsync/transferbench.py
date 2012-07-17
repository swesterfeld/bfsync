#!/usr/bin/python

import sys
import time
import subprocess
import argparse

from utils import *
from StatusLine import status_line, OutputSubsampler

def transfer_bench (args):
  parser = argparse.ArgumentParser (prog='bfsync transfer-bench')
  parser.add_argument ('--rsh', help='set remote shell')
  parser.add_argument ("host")
  parsed_args = parser.parse_args (args)

  if parsed_args.host == "__remote__":
    x = ""
    for i in xrange (1024 * 256 / 8):
      x += "foo/bar."                   # <- 8 bytes
    while True:
      sys.stdout.write (x)

  else:
    if parsed_args.rsh is not None:
      remote_shell = parsed_args.rsh
    else:
      remote_shell = "ssh"

    command = [remote_shell, parsed_args.host, "bfsync", "transfer-bench", "__remote__"]
    remote_p = subprocess.Popen (command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    read_bytes = 0
    start_time = time.time()

    outss = OutputSubsampler()

    def update_status_line():
      elapsed_time = max (time.time() - start_time, 1)
      bytes_per_sec = max (read_bytes / elapsed_time, 1)
      status_line.update ("%5s transferred   -   rate %s" % (format_size1 (read_bytes), format_rate (bytes_per_sec)))
    while True:
      read_bytes += len (remote_p.stdout.read (256 * 1024))
      if outss.need_update():
        update_status_line()
