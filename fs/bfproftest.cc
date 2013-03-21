// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bftimeprof.hh"
#include <stdio.h>
#include <unistd.h>

using namespace BFSync;

TimeProfSection tp_slow_func ("slow_func");

void
slow_func()
{
  TimeProfHandle h (tp_slow_func);

  usleep (50);
}

TimeProfSection tp_fast_func ("fast_func");

void
fast_func()
{
  TimeProfHandle h (tp_fast_func);

  usleep (10);
}

int
main()
{
  for (size_t i = 0; i < 1000; i++)
    {
      slow_func();
      fast_func();
    }
  printf ("%s", TimeProf::the()->result().c_str());
}
