// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bfgroup.hh"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

using namespace BFSync;

using std::string;

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      printf ("usage: bfgrouptest <pid>\n");
      exit (1);
    }
  
  string group = get_bfsync_group (atoi (argv[1]));
  printf ("''%s''\n", group.c_str());
}
