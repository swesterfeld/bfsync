// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

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
