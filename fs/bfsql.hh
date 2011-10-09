/*
  bfsync: Big File synchronization based on Git

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

namespace BFSync
{

int      sqlite_open (const std::string& filename);
sqlite3 *sqlite_db();

class SQLStatement
{
  sqlite3_stmt *stmt_ptr;
  bool          m_success;
public:
  SQLStatement (const std::string& sql);
  ~SQLStatement();

  void reset();
  void step();

  void bind_int (int pos, int value);

  bool success() const;
};

}

#endif
