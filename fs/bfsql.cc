/*
  bfsync: Big File synchronization based on Git - FUSE filesystem

  Copyright (C) 2011 Stefan Westerfeld

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

#include "bfsql.hh"

using std::string;
using std::map;

namespace BFSync
{

static sqlite3 *db_ptr;

int
sqlite_open (const string& filename)
{
  return sqlite3_open (filename.c_str(), &db_ptr);
}

sqlite3*
sqlite_db()
{
  return db_ptr;
}

SQLStatement::SQLStatement (const string& sql) :
  stmt_ptr (NULL),
  m_success (true)
{
  int rc = sqlite3_prepare_v2 (db_ptr, sql.c_str(), sql.size(), &stmt_ptr, NULL);
  if (rc != SQLITE_OK)
    m_success = false;
}

SQLStatement::~SQLStatement()
{
  // finalize
  sqlite3_finalize (stmt_ptr);
}

void
SQLStatement::begin()
{
  int rc = sqlite3_exec (sqlite_db(), "begin", NULL, NULL, NULL);

  if (rc != SQLITE_OK)
    m_success = false;
}

void
SQLStatement::commit()
{
  int rc = sqlite3_exec (sqlite_db(), "commit", NULL, NULL, NULL);

  if (rc != SQLITE_OK)
    m_success = false;
}

//---------------------------

SQLStatementStore::~SQLStatementStore()
{
  for (map<string, SQLStatement*>::const_iterator si = stmt_map.begin(); si != stmt_map.end(); si++)
    {
      if (si->second)
        delete si->second;
    }
  stmt_map.clear();
}

SQLStatement&
SQLStatementStore::get (const string& sql)
{
  SQLStatement*& result = stmt_map[sql];
  if (!result)
    result = new SQLStatement (sql);
  return *result;
}

}
