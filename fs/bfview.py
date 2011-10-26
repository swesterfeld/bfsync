#!/usr/bin/python

import sys

diff = sys.stdin.read()

sdiff = diff.split ("\0")

while len (sdiff) > 1:
  fcount = 0
  if sdiff[0] == "l+" or sdiff[0] == "l!":
    fcount = 4
  elif sdiff[0] == "l-":
    fcount = 3
  elif sdiff[0] == "i+" or sdiff[0] == "i!":
    fcount = 16
  elif sdiff[0] == "i-":
    fcount = 2

  if fcount == 0:
    print sdiff
  assert (fcount != 0)
  print "|".join (sdiff[0:fcount])
  sdiff = sdiff[fcount:]
