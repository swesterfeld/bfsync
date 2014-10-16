// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bfsyncdb.hh"

using std::string;
using std::vector;

using BFSync::BDB_TABLE_INODES;
using BFSync::BDB_TABLE_LINKS;
using BFSync::DataBuffer;
using BFSync::DbcPtr;

namespace {

class IntegrityCheck
{
  typedef boost::unordered_map<BFSync::ID, int> IDMap;

  BDBPtr    bdb_ptr;
  IDMap     id_map;

  void read_all_ids();
  void check_links();

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
  DbcPtr dbc (bdb_ptr.get_bdb());   // Acquire a cursor for the database.

  Dbt key;
  Dbt data;
  Dbt multi_data;

  std::vector<char> multi_data_buffer (64 * 1024);

  multi_data.set_flags (DB_DBT_USERMEM);
  multi_data.set_data (&multi_data_buffer[0]);
  multi_data.set_ulen (multi_data_buffer.size());

  ids_update_status (0);

  int dbc_ret = dbc->get (&key, &multi_data, DB_FIRST | DB_MULTIPLE_KEY);
  while (dbc_ret == 0)
    {
      DbMultipleKeyDataIterator data_iterator (multi_data);

      while (data_iterator.next (key, data))
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
      dbc_ret = dbc->get (&key, &multi_data, DB_NEXT | DB_MULTIPLE_KEY);
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

void
IntegrityCheck::check_links()
{
  DbcPtr dbc (bdb_ptr.get_bdb());   // Acquire a cursor for the database.

  Dbt key;
  Dbt data;
  Dbt multi_data;

  std::vector<char> multi_data_buffer (64 * 1024);

  multi_data.set_flags (DB_DBT_USERMEM);
  multi_data.set_data (&multi_data_buffer[0]);
  multi_data.set_ulen (multi_data_buffer.size());

  size_t n_links = 0;
  links_update_status (0);

  int dbc_ret = dbc->get (&key, &multi_data, DB_FIRST | DB_MULTIPLE_KEY);
  while (dbc_ret == 0)
    {
      DbMultipleKeyDataIterator data_iterator (multi_data);

      while (data_iterator.next (key, data))
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

              IDMap::iterator id_it;

              id_it = id_map.find (id);
              if (id_it == id_map.end())
                {
                  printf ("left side broken");
                }

              id_it = id_map.find (inode_id);
              if (id_it == id_map.end())
                {
                  printf ("right side broken");
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
              // printf ("%s -/%s/-> %s\n", id.pretty_str().c_str(), name.c_str(), inode_id.pretty_str().c_str());
              //id_map[id] = 0;
            }
        }
      dbc_ret = dbc->get (&key, &multi_data, DB_NEXT | DB_MULTIPLE_KEY);
    }
  links_update_status (n_links);
  printf ("\n");
}


vector<string>
IntegrityCheck::run()
{
  read_all_ids();
  check_links();

  vector<string> errs;
  errs.push_back ("foo");
  return errs;
}

vector<string>
check_inodes_links_integrity (BDBPtr bdb)
{
  IntegrityCheck integrity_check (bdb);

  return integrity_check.run();
}

