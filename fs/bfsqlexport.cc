/*
  bfsync: Big File synchronization tool

  Copyright (C) 2012 Stefan Westerfeld

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "bfsyncdb.hh"
#include "bfsyncfs.hh"

extern "C" {
// #include <Python.h>
extern int PyErr_CheckSignals();
}

using std::string;
using std::vector;

using BFSync::gettime;
using BFSync::string_printf;
using BFSync::DataBuffer;
using BFSync::DataOutBuffer;

SQLExport::SQLExport (BDBPtr bdb_ptr) :
  bdb_ptr (bdb_ptr)
{
  scan_ops = 0;
  start_time = gettime();
}

bool
same_data (const SQLExportData& d1, const SQLExportData& d2)
{
  return d1.filename == d2.filename && d1.type == d2.type && d1.size == d2.size && d1.hash == d2.hash;
}

bool
cmp_link_name (Link *l1, Link *l2)
{
  return l1->name < l2->name;
}

void
SQLExport::walk (const ID& id, const string& prefix, FILE *out_file)
{
  if (sig_interrupted)
    return;

  INode inode = bdb_ptr.load_inode (id, version);
  if (inode.valid)
    {
      string filename = prefix.size() ? prefix : "/";

      // write data
      out_buffer.clear();
      out_buffer.write_string (filename);
      out_buffer.write_uint32 (inode.type);
      out_buffer.write_string (inode.hash);
      out_buffer.write_uint64 (inode.size);
      // write prefix len
      len_buffer.clear();
      len_buffer.write_uint32 (out_buffer.size());

      fwrite (len_buffer.begin(), len_buffer.size(), 1, out_file);
      fwrite (out_buffer.begin(), out_buffer.size(), 1, out_file);

      maybe_split_transaction();
      scan_ops++;
      update_status();

      if (inode.type == BFSync::FILE_DIR)
        {
          vector<Link> links = bdb_ptr.load_links (id, version);

          // sort links before recursion
          vector<Link*> sorted_links (links.size());
          for (size_t l = 0; l < links.size(); l++)
            sorted_links[l] = &links[l];
          std::sort (sorted_links.begin(), sorted_links.end(), cmp_link_name);

          for (vector<Link*>::const_iterator li = sorted_links.begin(); li != sorted_links.end(); li++)
            {
              const Link* link = *li;
              const string& inode_name = prefix + "/" + link->name;

              walk (link->inode_id, inode_name, out_file);
            }
        }
    }
}

void
SQLExport::maybe_split_transaction()
{
  transaction_ops++;
  if (transaction_ops >= 20000)
    {
      transaction_ops = 0;

      bdb_ptr.commit_transaction();
      bdb_ptr.begin_transaction();
    }
}

void
SQLExport::update_status()
{
  const double time = gettime();
  if (fabs (time - last_status_time) > 1)
    {
      last_status_time = time;
      printf ("%d | %.2f scan_ops/sec\n", scan_ops, scan_ops / (time - start_time));

      if (!sig_interrupted && PyErr_CheckSignals())
        sig_interrupted = true;
    }
}

string
SQLExport::build_filelist (unsigned int version)
{
  if (filelist_map[version] != "")
    return filelist_map[version];

  string filelist_name = string_printf ("/tmp/bdb2sql.%d", version);
  filelist_map[version] = filelist_name;

  this->version = version;
  transaction_ops = 0;
  last_status_time = 0;

  FILE *file = fopen (filelist_name.c_str(), "w");
  assert (file);

  const double version_start_time = gettime();
  bdb_ptr.begin_transaction();
  walk (id_root(), "", file);
  bdb_ptr.commit_transaction();
  const double version_end_time = gettime();
  fclose (file);

  printf ("### time: %.2f\n", (version_end_time - version_start_time));
  return filelist_name;
}

bool
read_sxd (FILE *file, SQLExportData& data)
{
  if (feof (file))
    return true;

  char len_data[sizeof (guint32)];
  if (fread (len_data, sizeof (guint32), 1, file) == 1)
    {
      DataBuffer len_buffer (len_data, sizeof (guint32));
      guint32 len = len_buffer.read_uint32();

      char data_data[len];
      if (fread (data_data, len, 1, file) == 1)
        {
          DataBuffer data_buffer (data_data, len);

          data.filename = data_buffer.read_string();
          data.type = data_buffer.read_uint32();
          data.hash = data_buffer.read_string();
          data.size = data_buffer.read_uint64();
          return false;
        }
      else
        {
          return true;
        }
    }
  else
    {
      return true;
    }
}

void
SQLExport::export_version (unsigned int version)
{
  sig_interrupted = false;
  string old_files = build_filelist (version - 1);
  if (sig_interrupted)
    throw BDBException (BFSync::BDB_ERROR_INTR);

  string new_files = build_filelist (version);
  if (sig_interrupted)
    throw BDBException (BFSync::BDB_ERROR_INTR);

  vector<string> new_set;
  vector<string> keep_set;
  vector<string> deleted_set;
  vector<string> modified_set;

  FILE *old_f = fopen (old_files.c_str(), "r");
  FILE *new_f = fopen (new_files.c_str(), "r");

  bool old_eof = false;
  bool new_eof = false;

  SQLExportData old_data, new_data;
  enum { OLD, NEW, BOTH } next_read = BOTH;

  while (1)
    {
      if (next_read == OLD || next_read == BOTH)
        {
          old_eof = read_sxd (old_f, old_data);
        }
      if (next_read == NEW || next_read == BOTH)
        {
          new_eof = read_sxd (new_f, new_data);
        }
      if (old_eof && new_eof)
        break;

      if (old_eof)
        {
          new_set.push_back (new_data.filename);
          next_read = NEW;
        }
      else if (new_eof)
        {
          deleted_set.push_back (old_data.filename);
          next_read = OLD;
        }
      else
        {
          if (old_data.filename < new_data.filename)
            {
              deleted_set.push_back (old_data.filename);
              next_read = OLD;
            }
          else if (old_data.filename == new_data.filename)
            {
              if (same_data (old_data, new_data))
                {
                  keep_set.push_back (old_data.filename);
                }
              else
                {
                  modified_set.push_back (old_data.filename);
                }
              next_read = BOTH;
            }
          else  // new_data.filename < old_data.filename
            {
              new_set.push_back (new_data.filename);
              next_read = NEW;
            }
        }
    }
#if 0
  for (size_t i = 0; i < deleted_set.size(); i++)
    printf ("- %s\n", deleted_set[i].c_str());
  for (size_t i = 0; i < modified_set.size(); i++)
    printf ("! %s\n", modified_set[i].c_str());
  for (size_t i = 0; i < new_set.size(); i++)
    printf ("+ %s\n", new_set[i].c_str());
  for (size_t i = 0; i < keep_set.size(); i++)
    printf ("= %s\n", keep_set[i].c_str());
#endif
}
