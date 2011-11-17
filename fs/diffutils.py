
def links_from_table (c, version, id_start):
  d = dict()
  zeros = "0" * 38
  ffffs = "f" * 38
  c.execute ('''SELECT dir_id, name, inode_id FROM links
                  WHERE dir_id >= '%02x%s' and dir_id <= '%02x%s'
                  AND %d >= vmin AND %d <= vmax''' % (id_start, zeros, id_start, ffffs, version, version))
  for row in c:
    key = row[0:2]
    d[key] = row[2:]
  return d

def dict_from_table (c, table, n_keys, version, id_start_str):
  d = dict()
  c.execute ('''SELECT * FROM %s WHERE %s and %d >= vmin and %d <= vmax''' % (table, id_start_str, version, version))
  for row in c:
    key = row[2:2 + n_keys]
    d[key] = row[2 + n_keys:]
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

def diff (c, version_a, version_b, outfile):
  change_list = []

  for id_start in range (256):
    dict_a = links_from_table (c, version_a, id_start)
    dict_b = links_from_table (c, version_b, id_start)
    change_list += compute_changes ("l", dict_a, dict_b)

    zeros = "0" * 38
    ffffs = "f" * 38
    id_start_str = "id >= '%02x%s' and id <= '%02x%s'" % (id_start, zeros, id_start, ffffs)
    dict_a = dict_from_table (c, "inodes", 1, version_a, id_start_str)
    dict_b = dict_from_table (c, "inodes", 1, version_b, id_start_str)
    change_list += compute_changes ("i", dict_a, dict_b)

  # sort changes (for better compression)
  change_list.sort()

  # write changes to outfile
  for change in change_list:
    write1change (change, outfile)

