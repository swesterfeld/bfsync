// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "bfidhash.hh"
#include "bfbdb.hh"
#include "bfidsorter.hh"

#include <vector>
#include <algorithm>

using std::vector;

using BFSync::ID;
using BFSync::IDSorter;

int
main()
{
  IDSorter id_sorter;

  for (size_t prefix_count = 0; prefix_count < 1000; prefix_count++)
    {
      size_t prefix_len = g_random_int_range (1, 6);
      vector<char> path_prefix;
      for (size_t i = 0; i < prefix_len; i++)
        {
          path_prefix.push_back (g_random_int_range (0, 256));
        }
      for (size_t id_count = 0; id_count < 1000; id_count++)
        {
          ID id = ID::gen_new ("/");
          id.path_prefix = path_prefix;
          id_sorter.insert (id);
        }
    }
  id_sorter.sort();
  for (size_t i = 0; i < id_sorter.size(); i++)
    {
      ID id = id_sorter.id (i);
      //printf ("%s\n", id.pretty_str().c_str());
    }
  id_sorter.print_mem_usage();
}
