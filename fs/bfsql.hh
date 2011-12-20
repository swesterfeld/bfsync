/*
  bfsync: Big File synchronization tool

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

#ifndef BFSYNC_SQL_HH
#define BFSYNC_SQL_HH

#include <sqlite3.h>
#include <string>
#include <map>
#include <stdio.h>
#include "bfidhash.hh"

#define DEBUG_SQL (0)

namespace BFSync
{

int      sqlite_open (const std::string& filename);
sqlite3 *sqlite_db();

class SQLStatement
{
  sqlite3_stmt *stmt_ptr;
  bool          m_success;
#if DEBUG_SQL
  std::string   m_sql;
#endif
public:
  SQLStatement (const std::string& sql);
  ~SQLStatement();

  void begin();
  void commit();

  void
  reset()
  {
    int rc = sqlite3_reset (stmt_ptr);

    if (rc != SQLITE_OK)
      m_success = false;
  }

  int
  step()
  {
#if DEBUG_SQL
    printf ("%s\n", m_sql.c_str());
    fflush (stdout);
#endif
    int rc = sqlite3_step (stmt_ptr);

    if (rc != SQLITE_DONE)
      m_success = false;
    return rc;
  }

  bool
  success() const
  {
    return m_success;
  }

  // bind
  void
  bind_int (int pos, int value)
  {
    int rc = sqlite3_bind_int (stmt_ptr, pos, value);

    if (rc != SQLITE_OK)
      m_success = false;
  }

  void
  bind_text (int pos, const std::string& str)
  {
    int rc = sqlite3_bind_text (stmt_ptr, pos, str.c_str(), -1, SQLITE_TRANSIENT);

    if (rc != SQLITE_OK)
      m_success = false;
  }

  // columns
  int
  column_int (int pos) const
  {
    return sqlite3_column_int (stmt_ptr, pos);
  }
  const char*
  column_text (int pos) const
  {
    return (const char *) sqlite3_column_text (stmt_ptr, pos);
  }
  ID
  column_id (int pos) const
  {
    return ID ((const char *) sqlite3_column_text (stmt_ptr, pos));
  }
};

class SQLStatementStore
{
  std::map<std::string, SQLStatement *> stmt_map;
public:
  ~SQLStatementStore();

  SQLStatement& get (const std::string& str);
};

}

#endif
