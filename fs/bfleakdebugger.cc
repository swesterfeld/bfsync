// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bfleakdebugger.hh"
#include <assert.h>
#include <glib.h>

#define DEBUG (1)

using namespace BFSync;

using std::string;
using std::map;
using std::vector;

void
LeakDebugger::ptr_add (void *p)
{
  if (DEBUG)
    {
      Lock lock (mutex);

      int& p_map_entry = ptr_map[p];

      if (p_map_entry != 0)
        g_critical ("LeakDebugger: invalid registration of object type %s detected; ptr_map[p] is %d\n",
                    type.c_str(), p_map_entry);

      p_map_entry++;
    }
}

void
LeakDebugger::ptr_del (void *p)
{
  if (DEBUG)
    {
      Lock lock (mutex);

      boost::unordered_map<void *, int>::iterator pi = ptr_map.find (p);
      if (pi == ptr_map.end())
        {
          g_critical ("LeakDebugger: invalid deletion of object type %s detected; ptr_map entry not found\n",
                      type.c_str());
          return;
        }

      int& p_map_entry = pi->second;
      if (p_map_entry != 1)
        g_critical ("LeakDebugger: invalid deletion of object type %s detected; ptr_map[p] is %d\n",
                    type.c_str(), p_map_entry);

      ptr_map.quick_erase (pi);
      /*
       * hashmap erase performance is poor if the hashmap has a huge number
       * of buckets and only a small part of all buckets is used, so we rehash
       * if the load factor is four times smaller than the maximum load factor
       */
      if (ptr_map.load_factor() / ptr_map.max_load_factor() < 0.25)
        ptr_map.rehash (0);
    }
}

vector<LeakDebugger*> leak_debuggers;

LeakDebugger::LeakDebugger (const string& name) :
  type (name)
{
  leak_debuggers.push_back (this);
}

LeakDebugger::~LeakDebugger()
{
  if (DEBUG)
    {
      int alive = 0;

      for (boost::unordered_map<void *, int>::iterator pi = ptr_map.begin(); pi != ptr_map.end(); pi++)
        {
          if (pi->second != 0)
            {
              assert (pi->second == 1);
              alive++;
            }
        }
      if (alive)
        {
          g_printerr ("LeakDebugger (%s) => %d objects remaining\n", type.c_str(), alive);
        }
    }
}

void
LeakDebugger::print_stats()
{
  for (vector<LeakDebugger*>::iterator ldi = leak_debuggers.begin(); ldi != leak_debuggers.end(); ldi++)
    {
      Lock lock ((*ldi)->mutex);
      printf ("%s: %zd\n", (*ldi)->type.c_str(), (*ldi)->ptr_map.size());
    }
}
