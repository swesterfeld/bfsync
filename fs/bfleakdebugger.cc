/*
 * Copyright (C) 2011 Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bfleakdebugger.hh"
#include <assert.h>
#include <glib.h>

#define DEBUG (1)

using namespace BFSync;

using std::string;
using std::map;

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
    }
}

LeakDebugger::LeakDebugger (const string& name) :
  type (name)
{
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
