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

using std::string;
using std::vector;

using BFSync::gettime;
using BFSync::string_printf;
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
  return d1.filename == d2.filename;
}

bool
cmp_link_name (Link *l1, Link *l2)
{
  return l1->name < l2->name;
}

void
SQLExport::walk (const ID& id, const string& prefix, FILE *out_file)
{
  INode inode = bdb_ptr.load_inode (id, version);
  if (inode.valid)
    {
      string filename = prefix.size() ? prefix : "/";

      DataOutBuffer out_buffer;
      out_buffer.write_string (filename);
      fwrite (out_buffer.begin(), out_buffer.size(), 1, out_file);
#if 0
      SQLExportData old_data = bdb_ptr.sql_export_get (filename);

      SQLExportData data;
      data.valid = true;
      data.filename = filename;

      bool need_write = false;
      if (old_data.valid)
        {
          if (same_data (data, old_data))
            {
              // printf ("%-50s - same\n", filename.c_str());
            }
          else
            {
              need_write = true;
              // printf ("%-50s - changed\n", filename.c_str());
            }
        }
      else
        {
          need_write = true;
          // printf ("%-50s - new\n", filename.c_str());
        }

      if (need_write)
        bdb_ptr.sql_export_set (data);
#endif

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
    }
}

void
SQLExport::export_version (unsigned int version)
{
  this->version = version;
  transaction_ops = 0;
  last_status_time = 0;

  FILE *file = fopen (string_printf ("/tmp/bdb2sql.%d", version).c_str(), "w");
  assert (file);

  const double version_start_time = gettime();
  bdb_ptr.begin_transaction();
  walk (id_root(), "", file);
  bdb_ptr.commit_transaction();
  const double version_end_time = gettime();
  fclose (file);

  printf ("### time: %.2f\n", (version_end_time - version_start_time));
}
