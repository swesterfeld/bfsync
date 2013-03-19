// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

int
main (int argc, char **argv)
{
  if (argc == 4 && strcmp (argv[1], "truncate") == 0)
    {
      if (truncate (argv[2], atoi (argv[3])) == 0)
        return 0;
      else
        {
          printf ("truncate error: %s\n", strerror (errno));
        }
    }
  return 1;
}
