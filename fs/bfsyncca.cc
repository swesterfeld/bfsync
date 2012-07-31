/*
  bfsync: Big File synchronization tool - CPU Affinity Helper

  Copyright (C) 2012 Stefan Westerfeld

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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <glib.h>

int
main (int argc, char **argv)
{
  if (argc > 2)
    {
      cpu_set_t cpus;
      CPU_ZERO (&cpus);
      CPU_SET (atoi (argv[1]), &cpus);
      sched_setaffinity (0, sizeof (cpu_set_t), &cpus);
      execvp (argv[2], argv + 2);
      perror (g_strdup_printf ("bfsyncca: %s", argv[2]));
      exit (1);
    }
  else if (argc == 1)
    {
      cpu_set_t cpus;
      CPU_ZERO (&cpus);
      sched_getaffinity (getpid(), sizeof (cpu_set_t), &cpus);
      printf ("CPU affinity mask (%d cpus found)\n", CPU_COUNT (&cpus));
      for (int cpu = 0; cpu < CPU_COUNT (&cpus); cpu++)
        {
          printf (" - cpu %d\n", cpu);
        }
      exit (0);
    }
  printf ("parse error in command line args\n");
  exit (1);
}
