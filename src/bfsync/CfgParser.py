#!/usr/bin/python

# bfsync: Big File synchronization tool
# Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

import re, sys

class CfgParser:
  TOKEN_IDENTIFIER = 1
  TOKEN_STRING = 2
  TOKEN_SEMICOLON = 3
  TOKEN_OPEN = 4
  TOKEN_CLOSE = 5
  TOKEN_EOF = 6
  TOKEN_ERROR = 7

  def get_token (self):
    m = re.match ("([a-zA-Z0-9_:.@/-]+)(.*)", self.toparse, re.DOTALL)
    if m:
      self.toparse = m.group (2)
      return self.TOKEN_IDENTIFIER, m.group (1)

    m = re.match ("[\t ]+(.*)", self.toparse, re.DOTALL)
    if m:
      self.toparse = m.group (1)
      return self.get_token()

    m = re.match ("\n(.*)", self.toparse, re.DOTALL)
    if m:
      self.toparse = m.group (1)
      self.line += 1
      return self.get_token()

    m = re.match ("#[^\n]*\n(.*)", self.toparse, re.DOTALL)
    if m:
      self.toparse = m.group (1)
      self.line += 1
      return self.get_token()

    m = re.match (";(.*)", self.toparse, re.DOTALL)
    if m:
      self.toparse = m.group (1)
      return self.TOKEN_SEMICOLON, ";"

    m = re.match ("{(.*)", self.toparse, re.DOTALL)
    if m:
      self.toparse = m.group (1)
      return self.TOKEN_OPEN, "{"

    m = re.match ("}(.*)", self.toparse, re.DOTALL)
    if m:
      self.toparse = m.group (1)
      return self.TOKEN_CLOSE, "}"

    m = re.match ('"(.*)', self.toparse, re.DOTALL)
    if m:
      self.toparse = m.group (1)
      out = ""
      while self.toparse != "" and self.toparse[0] != '"':
        if self.toparse[0] == "\\":         # \" => ",   \\ => \,  \a => a
          self.toparse = self.toparse[1:]
        if len (self.toparse) > 0:
          out += self.toparse[0]
        self.toparse = self.toparse[1:]
      self.toparse = self.toparse[1:]
      return self.TOKEN_STRING, out

    if self.toparse == "":
      return self.TOKEN_EOF, ""

    return self.TOKEN_ERROR, "<NONE>"

  def parse_error (self, message):
    print "%s:%d: parse error: %s" % (self.filename, self.line, message)
    sys.exit (1)

  def parse_group_or_key_value (self, key):
    token, token_value = self.get_token()
    if token == CfgParser.TOKEN_OPEN:
      if not key in self.allow_groups:
        self.parse_error ("group '%s' is not a valid group" % key)
      self.parse_group (key)
    elif token == CfgParser.TOKEN_STRING or token == CfgParser.TOKEN_IDENTIFIER:
      values = [ token_value ]
      while 1:
        token, token_value = self.get_token()
        if token == CfgParser.TOKEN_SEMICOLON:
          if not key in self.allow_keys:
            self.parse_error ("key '%s' is not a valid key" % key)
          self.values[key] = values;
          break;
        if token == CfgParser.TOKEN_STRING or token == CfgParser.TOKEN_IDENTIFIER:
          values += [ token_value ]
        else:
          self.parse_error ("key '%s', expected string/identifier or ';'" % key)
    else:
      self.parse_error("key/group '%s', expected string/identifier or '{'" % key)

  def parse_group (self, group_name):
    token, token_value = self.get_token()
    if token == CfgParser.TOKEN_IDENTIFIER:
      self.parse_group_or_key_value (group_name + "/" + token_value)
      self.parse_group (group_name)
    elif token == CfgParser.TOKEN_CLOSE:
      return
    else:
      self.parse_error ("group '%s', expected identifier or '}'" % group_name)

  def __init__ (self, filename, allow_groups, allow_keys):
    self.values = dict()
    self.allow_keys = allow_keys
    self.allow_groups = allow_groups
    self.filename = filename

    # read config file
    f = open (filename)
    self.toparse = f.read()
    f.close()
    self.line = 1

    # parse config file
    while 1:
      token, token_value = self.get_token()
      if token == CfgParser.TOKEN_EOF:
        break;
      elif token == CfgParser.TOKEN_IDENTIFIER:
        self.parse_group_or_key_value (token_value)
      else:
        self.parse_error ("expected identifier or EOF")

  def get (self, key):
    if not self.values.has_key (key):
      return []
    else:
      return self.values[key]

  def set (self, key, values):
    if key not in self.allow_keys:
      raise Exception ("unsupported key %s" % key)
    self.values[key] = values

  def unset (self, key):
    if key not in self.allow_keys:
      raise Exception ("unsupported key %s" % key)
    if key in self.values:
      del self.values[key]

  def to_string (self):
    def maybe_quote (value):
      if re.match ("^[a-zA-Z0-9_:.@/-]+$", value):
        return value
      else:
        result = ""
        for c in value:
          if c == '"' or c == '\\':
            result += '\\'
          result += c
        return '"' + result + '"'

    str = ""
    last_group = ""
    for key in sorted (self.values.keys()):
      split_key = key.split ("/")
      if len (split_key) == 2:
        key_group = split_key[0]
        key_key   = split_key[1]
      else:
        key_group = ""
        key_key   = key

      if last_group != key_group:
        if last_group != "":
          str += "}\n"
        if key_group != "":
          str += key_group + " {\n"
        last_group = key_group

      values = self.values[key]
      if len (values) > 0:
        # add indentation in groups
        if key_group != "":
          str += "  ";
        str += key_key
        for value in values:
          str += " %s" % maybe_quote (value)
        str += ";\n"
    if last_group != "":
      str += "}\n"
    return str
