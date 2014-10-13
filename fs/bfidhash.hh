// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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

inline bool
operator!= (const ID& x, const ID& y)
{
  return !(x == y);
}

inline
ID::ID (const ID& id) :
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

inline size_t
hash_value (const ID& id)
{
  return id.a;
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

inline void
uint8_hex (guint8 value, char *dest)
{
  const char *hex_nibble = "0123456789abcdef";

  dest[0] = hex_nibble [(value >> 4) & 0xf];
  dest[1] = hex_nibble [value  & 0xf];
}

inline void
uint32_hex (guint32 value, char *dest)
{
  const char *hex_nibble = "0123456789abcdef";

  dest[0] = hex_nibble [(value >> 28) & 0xf];
  dest[1] = hex_nibble [(value >> 24) & 0xf];
  dest[2] = hex_nibble [(value >> 20) & 0xf];
  dest[3] = hex_nibble [(value >> 16) & 0xf];
  dest[4] = hex_nibble [(value >> 12) & 0xf];
  dest[5] = hex_nibble [(value >> 8) & 0xf];
  dest[6] = hex_nibble [(value >> 4) & 0xf];
  dest[7] = hex_nibble [value  & 0xf];
}

}

#endif
