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

using std::string;
using std::vector;

using BFSync::gettime;

SQLExport::SQLExport (BDBPtr bdb_ptr) :
  bdb_ptr (bdb_ptr)
{
}

bool
same_data (const SQLExportData& d1, const SQLExportData& d2)
{
  return d1.filename == d2.filename;
}

void
SQLExport::walk (const ID& id, const string& prefix)
{
  INode inode = bdb_ptr.load_inode (id, version);
  if (inode.valid)
    {
      string filename = prefix.size() ? prefix : "/";

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

      maybe_split_transaction();

      if (inode.type == BFSync::FILE_DIR)
        {
          vector<Link> links = bdb_ptr.load_links (id, version);
          for (vector<Link>::const_iterator li = links.begin(); li != links.end(); li++)
            {
              const string& inode_name = prefix + "/" + li->name;
              walk (li->inode_id, inode_name);
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
SQLExport::export_version (unsigned int version)
{
  this->version = version;
  transaction_ops = 0;

  const double start_t = gettime();
  bdb_ptr.begin_transaction();
  walk (id_root(), "");
  bdb_ptr.commit_transaction();
  const double end_t = gettime();

  printf ("### time: %.2f\n", (end_t - start_t));
}
