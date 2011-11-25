
def links_from_table (c, version, id_start_str):
  if id_start_str != "":
    id_start_str += " AND";
  d = dict()
  c.execute ('''SELECT dir_id, name, inode_id FROM links
                  WHERE %s %d >= vmin AND %d <= vmax''' % (id_start_str, version, version))
  for row in c:
    key = row[0:2]
    d[key] = row[2:]
  return d

def inodes_from_table (c, version, id_start_str):
  if id_start_str != "":
    id_start_str += " AND";
  d = dict()
  c.execute ('''SELECT * FROM inodes WHERE %s %d >= vmin AND %d <= vmax''' % (id_start_str, version, version))
  for row in c:
    key = row[2:3]
    d[key] = row[3:]
  return d

def key_to_str (k):
  s = ""
  for x in k:
    if s == "":
      s += "%s" % x
    else:
      s += " %s" % x
  return s

def mklist (k):
  l = []
  for x in k:
    l += [ "%s" % x ]
  return l

def write1change (change_list, outfile):
  for s in change_list:
    outfile.write (s + "\0")

def compute_changes (change_type, dict_a, dict_b):
  change_list = []

  for k in dict_a:
    if k in dict_b:
      if dict_a[k] == dict_b[k]:
        # identical
        pass
      else:
        # attribute change
        change = [ change_type + "!"] + mklist (k)
        for i in range (len (dict_b[k])):
          if dict_a[k][i] == dict_b[k][i]:
            change += [ "" ]
          else:
            change += [ "%s" % dict_b[k][i] ]
        change_list += [ change ]
    else:
      # entry deleted
      change_list += [ [ change_type + "-" ] + mklist (k) ]

  for k in dict_b:
    if k in dict_a:
      # attribute change <-> handled above
      pass
    else:
      # entry added
      change_list += [ [ change_type + "+" ] + mklist (k) + mklist (dict_b[k]) ]

  return change_list

def get_n_entries (c, version_a, version_b):
  c.execute ("""SELECT COUNT(*) FROM links WHERE (%d >= vmin AND %d <= vmax) OR (%d >= vmin AND %d <= vmax)""" %
             (version_a, version_a, version_b, version_b))
  for row in c:
    return row[0]

  return 0

def diff (c, version_a, version_b, outfile):
  change_list = []

  n_entries = get_n_entries (c, version_a, version_b)

  if n_entries < 50000:
    dict_a = links_from_table (c, version_a, "")
    dict_b = links_from_table (c, version_b, "")
    change_list += compute_changes ("l", dict_a, dict_b)

    dict_a = inodes_from_table (c, version_a, "")
    dict_b = inodes_from_table (c, version_b, "")
    change_list += compute_changes ("i", dict_a, dict_b)
  else:
    for id_start in range (256):
      zeros = "0" * 38
      ffffs = "f" * 38

      id_start_str = "dir_id >= '%02x%s' AND dir_id <= '%02x%s'" % (id_start, zeros, id_start, ffffs)
      dict_a = links_from_table (c, version_a, id_start_str)
      dict_b = links_from_table (c, version_b, id_start_str)
      change_list += compute_changes ("l", dict_a, dict_b)

      id_start_str = "id >= '%02x%s' AND id <= '%02x%s'" % (id_start, zeros, id_start, ffffs)
      dict_a = inodes_from_table (c, version_a, id_start_str)
      dict_b = inodes_from_table (c, version_b, id_start_str)
      change_list += compute_changes ("i", dict_a, dict_b)

  # sort changes (for better compression)
  change_list.sort()

  # write changes to outfile
  for change in change_list:
    write1change (change, outfile)

