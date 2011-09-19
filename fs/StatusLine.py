import sys

class StatusLine:
  def __init__ (self):
    self.op_text = ""
    self.line = ""
  def set_op (self, op):
    self.op_text = op + ": "
  def update (self, text):
    print "\r",
    # clear old status
    for i in range (len (self.line)):
      sys.stdout.write (" ")
    self.line = self.op_text + text
    print "\r%s " % self.line,
    sys.stdout.flush()
  def die (self, text):
    if len (self.line) > 0:
      print "\n"
    sys.stderr.write ("bfsync: %s\n" % text)
    sys.exit (1)

status_line = StatusLine()
