/*
  bfsync: Big File synchronization tool - FUSE filesystem

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

#ifndef BFSYNC_ID_HASH_HH
#define BFSYNC_ID_HASH_HH

#include <glib.h>
#include <string>
#include <vector>

namespace BFSync
{

class DataBuffer;
class DataOutBuffer;

struct ID
{
  std::vector<char> path_prefix;
  guint32 a, b, c, d, e;

  ID();
  ID (const ID& id);
  ID (DataBuffer& dbuf);
  ID (const std::string& str);

  ID& operator= (const ID& id);

  std::string no_prefix_str() const;
  std::string str() const;
  std::string pretty_str() const;

  void store (DataOutBuffer& data_buf) const;

  static ID gen_new (const char *path);
  static ID root();
};

inline bool
operator< (const ID& x, const ID& y)
{
  if (x.a != y.a)
    return x.a < y.a;

  if (x.b != y.b)
    return x.b < y.b;

  if (x.c != y.c)
    return x.c < y.c;

  if (x.d != y.d)
    return x.d < y.d;

  if (x.e != y.e)
    return x.e < y.e;

  // for in-memory order, locality doesn't matter much, so we compare path_prefix as
  // last (not first) step
  return x.path_prefix < y.path_prefix;
}

inline bool
operator== (const ID& x, const ID& y)
{
  return (x.a == y.a) && (x.b == y.b) && (x.c == y.c) && (x.d == y.d) && (x.e == y.e) && (x.path_prefix == y.path_prefix);
}

inline ID::ID (const ID& id) :
  path_prefix (id.path_prefix),
  a (id.a),
  b (id.b),
  c (id.c),
  d (id.d),
  e (id.e)
{
}

inline ID&
ID::operator= (const ID& id)
{
  path_prefix = id.path_prefix;
  a = id.a;
  b = id.b;
  c = id.c;
  d = id.d;
  e = id.e;

  return *this;
}

inline unsigned char
from_hex_nibble (char c)
{
  int uc = (unsigned char)c;

  if (uc >= '0' && uc <= '9') return uc - (unsigned char)'0';
  if (uc >= 'a' && uc <= 'f') return uc + 10 - (unsigned char)'a';
  if (uc >= 'A' && uc <= 'F') return uc + 10 - (unsigned char)'A';

  return 16;    // error
}

}

#endif
