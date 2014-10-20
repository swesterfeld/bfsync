// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bfsyncdb.hh"

using std::string;
using std::vector;

using BFSync::BDB_TABLE_INODES;
using BFSync::BDB_TABLE_LINKS;
using BFSync::DataBuffer;
using BFSync::DbcPtr;
using BFSync::BDB;
using BFSync::AllRecordsIterator;
using BFSync::string_printf;

#define VMSTR(v) ((v == VERSION_INF) ? "INF" : string_printf ("%u", v).c_str())

namespace {

class IntegrityCheck
{
  typedef boost::unordered_map<BFSync::ID, int> IDMap;

  BDBPtr          bdb_ptr;
  IDMap           id_map;

  vector<string>  errors;

  void read_all_ids();
  void check_links();
  void check_unreachable_inodes();

public:
  IntegrityCheck (BDBPtr bdb_ptr);

  vector<string> run();
};

}

IntegrityCheck::IntegrityCheck (BDBPtr bdb_ptr) :
  bdb_ptr (bdb_ptr)
{
}

static bool
output_needs_update()
{
  static int last_time = 0;
  int t = time (NULL);
  if (t != last_time)
    {
      last_time = t;
      return true;
    }
  return false;
}

static void
ids_update_status (size_t n_ids)
{
  printf ("\rINTEGRITY: phase 1/2: reading inode IDs: %zd", n_ids);
  fflush (stdout);
}

void
IntegrityCheck::read_all_ids()
{
  DbcPtr              dbc (bdb_ptr.get_bdb());   // Acquire a cursor for the database.
  AllRecordsIterator  ari (dbc.dbc);

  ids_update_status (0);

  Dbt key, data;
  while (ari.next (key, data))
    {
      char table = ((char *) key.get_data()) [key.get_size() - 1];
      if (table == BDB_TABLE_INODES)
        {
          DataBuffer kbuffer ((char *) key.get_data(), key.get_size());

          BFSync::ID id (kbuffer);
          id_map[id] = 0;

          if (output_needs_update())
            ids_update_status (id_map.size());
        }
    }
  ids_update_status (id_map.size());
  printf ("\n");
}

static void
links_update_status (size_t n_links)
{
  printf ("\rINTEGRITY: phase 2/2: checking links: %zd", n_links);
  fflush (stdout);
}

enum LinkErr {
  ERR_NONE = 0,
  ERR_DIR_ID = 1,
  ERR_INODE_ID = 2
};

static const char *
link_err2str (LinkErr err)
{
  if (err == ERR_DIR_ID)
    return "Directory ID not found in INode table";
  if (err == ERR_INODE_ID)
    return "INode ID not found in INode table";
  if (err == LinkErr (ERR_DIR_ID | ERR_INODE_ID))
    return "Directory ID and INode ID not found in INode table";
  return 0;
}

void
IntegrityCheck::check_links()
{
  DbcPtr              dbc (bdb_ptr.get_bdb());   // Acquire a cursor for the database.
  AllRecordsIterator  ari (dbc.dbc);


  size_t n_links = 0;
  links_update_status (0);

  Dbt key, data;
  while (ari.next (key, data))
    {
      char table = ((char *) key.get_data()) [key.get_size() - 1];
      if (table == BDB_TABLE_LINKS)
        {
          DataBuffer kbuffer ((char *) key.get_data(), key.get_size());
          BFSync::ID id (kbuffer);

          DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

          unsigned int vmin = dbuffer.read_uint32();
          unsigned int vmax = dbuffer.read_uint32();
          BFSync::ID inode_id (dbuffer);
          string name = dbuffer.read_string();

          LinkErr err = ERR_NONE;

          IDMap::iterator id_it;

          id_it = id_map.find (id);
          if (id_it == id_map.end())
            {
              err = LinkErr (ERR_DIR_ID | err);
            }

          id_it = id_map.find (inode_id);
          if (id_it == id_map.end())
            {
              err = LinkErr (ERR_INODE_ID | err);
            }
          else
            {
              // ID is reachable
              int& flags = id_it->second;
              flags |= 1;
            }

          n_links++;
          if (output_needs_update())
            links_update_status (n_links);

          if (err)
            {
              errors.push_back (string_printf ("LINK ERROR: %s {\n  %s=%u|%s|%s|%s\n}", link_err2str (err),
                 id.pretty_str().c_str(), vmin, VMSTR (vmax), inode_id.pretty_str().c_str(), name.c_str()));
            }
        }
    }
  links_update_status (n_links);
  printf ("\n");
}

void
IntegrityCheck::check_unreachable_inodes()
{
  for (IDMap::const_iterator id_it = id_map.begin(); id_it != id_map.end(); id_it++)
    {
      const BFSync::ID& id  = id_it->first;
      const int flags       = id_it->second;

      if (flags == 0 && id != BFSync::ID::root())
        errors.push_back (string_printf ("INODE ERROR: INode ID '%s' is unreachable", id.pretty_str().c_str()));
    }
}

vector<string>
IntegrityCheck::run()
{
  read_all_ids();
  check_links();
  check_unreachable_inodes();

  return errors;
}

vector<string>
check_inodes_links_integrity (BDBPtr bdb)
{
  IntegrityCheck integrity_check (bdb);

  return integrity_check.run();
}

