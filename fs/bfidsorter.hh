// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#ifndef BFSYNC_ID_SORTER_HH
#define BFSYNC_ID_SORTER_HH

#include "bfbdb.hh"

#include <algorithm>

namespace BFSync
{

class IDSorter
{
  std::vector<size_t> id_pos;
  DataOutBuffer       id_buf;

  struct IDSorterCmp
  {
    IDSorter *sorter;
    inline bool
    operator() (size_t p1, size_t p2) const
    {
      return sorter->less (p1, p2);
    }
  };

public:
  void
  insert (const ID& id)
  {
    id_pos.push_back (id_buf.size());
    id.store (id_buf);
  }
  void
  sort()
  {
    IDSorterCmp cmp;
    cmp.sorter = this;
    std::sort (id_pos.begin(), id_pos.end(), cmp);
  }
  bool
  less (size_t p1, size_t p2)
  {
    const char *mem1 = id_buf.begin() + p1;
    const char *mem2 = id_buf.begin() + p2;
    size_t l1 = strlen (mem1) + 1 + 20;
    size_t l2 = strlen (mem2) + 1 + 20;
    int r = memcmp  (mem1, mem2, std::min (l1, l2));
    if (r == 0)
      {
        if (l1 != l2)
          return l1 < l2;
        else
          return false;
      }
    else
      {
        return r < 0;
      }
  }
  size_t
  size() const
  {
    return id_pos.size();
  }
  ID
  id (size_t pos)
  {
    DataBuffer db (id_buf.begin() + id_pos[pos], 100000);
    return ID (db);
  }
  void
  print_mem_usage()
  {
    printf ("data:     %zd\n", id_buf.size());
    printf ("index:    %zd\n", id_pos.size() * sizeof (id_pos[0]));
  }
};

}

#endif /* BFSYNC_ID_SORTER_HH */
