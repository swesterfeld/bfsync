/*
  bfsync: Big File synchronization based on Git - FUSE filesystem

  Copyright (C) 2011 Stefan Westerfeld

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

#include "bfsyncserver.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#include <string>

using namespace BFSync;

using std::string;

Server::Server() :
  socket_ok (false),
  socket_fd (-1)
{
}

Server::~Server()
{
  if (socket_ok)
    {
      close (socket_fd);
      unlink (socket_path.c_str());
    }
}

bool
Server::init_socket (const string& repo_path)
{
  assert (!socket_ok);

  socket_fd = socket (PF_UNIX, SOCK_STREAM, 0);
  if (socket_fd < 0)
    {
      printf ("bfsyncfs: creation of unix domain socket failed\n");
      return false;
    }

  if (fcntl (socket_fd, F_SETFL, O_NONBLOCK) < 0)
    {
      printf ("bfsyncfs: can't initialize non blocking I/O on socket\n");
      close (socket_fd);
      return false;
    }

  socket_path = repo_path + "/.bfsync/socket";

  struct sockaddr_un socket_addr;

  int maxlen = sizeof (socket_addr.sun_path);
  strncpy (socket_addr.sun_path, socket_path.c_str(), maxlen);
  socket_addr.sun_path[maxlen-1] = 0;
  socket_addr.sun_family = AF_UNIX;

  if (bind (socket_fd, (struct sockaddr *) &socket_addr, sizeof (struct sockaddr_un)) < 0)
    {
      printf ("bfsyncfs: can't bind socket to file %s\n", socket_path.c_str());
      close (socket_fd);
      return false;
    }

  if (listen (socket_fd, 16) < 0)
    {
      printf ("bfsyncfs: can't listen on the socket");
      close (socket_fd);
      return false;
    }

  socket_ok = true;
  return true;

}
