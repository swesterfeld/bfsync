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
#include "bfsyncfs.hh"
#include "bfgitfile.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <glib.h>

#include <string>
#include <vector>

using namespace BFSync;

using std::string;
using std::vector;

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

void*
thread_start (void *arg)
{
  Server *instance = static_cast<Server *> (arg);
  instance->run();
  return NULL;
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

void
Server::start_thread()
{
  if (socket_ok)
    pthread_create (&thread, NULL, thread_start, this);
}

void
Server::run()
{
  debug ("Server::run()\n");
  struct pollfd poll_fds[1];

  poll_fds[0].fd = socket_fd;
  poll_fds[0].events = POLLIN;
  poll_fds[0].revents = 0;

  while (1)
    {
      if (poll (poll_fds, 1, -1) > 0)
        {
          struct sockaddr_un incoming;
          socklen_t size_in = sizeof (struct sockaddr_un);

          int client_fd = accept (socket_fd, (struct sockaddr*) &incoming, &size_in);
          if (client_fd > 0)
            {
              debug ("Server: handle_client (%d)\n", client_fd);
              handle_client (client_fd);
            }
        }
    }
}

bool
Server::decode (const vector<char>& buffer, vector<string>& request)
{
  string client_req_len;
  int    len = -1;
  for (size_t i = 0; i < buffer.size(); i++)
    {
      if (buffer[i] == 0)
        {
          len = atoi (&buffer[0]);
          break;
        }
    }
  if (len == -1)
    return false;

  size_t skip = strlen (&buffer[0]) + 1; // skip len field
  if (buffer.size() != skip + len)
    return false;

  request.clear();
  string s;
  for (size_t i = skip; i < buffer.size(); i++)
    {
      if (buffer[i] == 0)
        {
          request.push_back (s);
          s = "";
        }
      else
        {
          s += buffer[i];
        }
    }
  return true;
}

void
Server::encode (const vector<string>& reply, vector<char>& buffer)
{
  buffer.clear();

  // compute reply size
  size_t reply_size = 0;
  for (size_t i = 0; i < reply.size(); i++)
    reply_size += reply[i].size() + 1;

  // build buffer
  char *lstr = g_strdup_printf ("%zd", reply_size);
  string l = lstr;
  g_free (lstr);

  buffer.resize (l.size() + 1 + reply_size);

  // len => buffer
  vector<char>::iterator bi = buffer.begin();
  std::copy (l.begin(), l.end(), bi);
  bi += l.size() + 1;

  for (size_t i = 0; i < reply.size(); i++)
    {
      // reply[i] => buffer
      std::copy (reply[i].begin(), reply[i].end(), bi);
      bi += reply[i].size() + 1;
    }
}

void
Server::handle_client (int client_fd)
{
  struct pollfd cpoll_fds[1];
  FSLock *lock = 0;

  vector<char> client_req;

  while (client_fd > 0)
    {
      cpoll_fds[0].fd = client_fd;
      cpoll_fds[0].events = POLLIN;
      cpoll_fds[0].revents = 0;
      if (poll (cpoll_fds, 1, -1) > 0)
        {
          char buffer[1024];
          int len = read (client_fd, buffer, 1024);

          if (len == 0 /* remote end closed connection */
          || (len == -1 && errno != EAGAIN && errno != EINTR)) /* some error */
            {
              close (client_fd);
              client_fd = -1;
            }
          else
            {
              client_req.insert (client_req.end(), buffer, buffer + len);
            }
        }
      debug ("Server: client_req.size() = %zd\n", client_req.size());
      vector<string> request;
      if (decode (client_req, request))
        {
          /*
          printf ("REQUEST:\n");
          for (vector<string>::iterator ri = request.begin(); ri != request.end(); ri++)
            {
              printf (" - %s\n", ri->c_str());
            }
          */
          vector<string> result;
          if (request.size() > 0)
            {
              if (request[0] == "print")
                {
                  for (size_t i = 1; i < request.size(); i++)
                    printf ("%s", request[i].c_str());
                  result.push_back ("ok");
                }
              else if (request[0] == "get-lock")
                {
                  if (lock)
                    result.push_back ("fail: lock already acquired");
                  else
                    {
                      lock = new FSLock (FSLock::RDONLY);
                      result.push_back ("ok");
                    }
                }
              else if (request[0] == "add-new")
                {
                  FSLock add_lock (FSLock::REORG);
                  bool ok = true;

                  for (size_t i = 1; i + 1 < request.size(); i += 2)
                    {
                      string filename   = request[i];
                      string hash       = request[i + 1];
                      string error_msg  = "";

                      if (!add_file (filename, hash, error_msg))
                        {
                          ok = false;
                          char *error = g_strdup_printf ("fail: error while adding file '%s', hash '%s'\n%s",
                                                         filename.c_str(), hash.c_str(), error_msg.c_str());
                          result.push_back (error);
                          g_free (error);
                          break;
                        }
                    }
                  if (ok)
                    result.push_back ("ok");
                }
            }
          vector<char> rbuffer;
          encode (result, rbuffer);
          write (client_fd, &rbuffer[0], rbuffer.size());
          client_req.clear();
        }
    }
  if (lock)
    {
      delete lock;
      lock = NULL;
    }
}

bool
Server::add_file (const string& filename, const string& hash, string& error)
{
  GitFile gf;
  string git_filename = Options::the()->repo_path + "/git/files/" + name2git_name (filename);
  string new_filename = Options::the()->repo_path + "/new/" + filename;
  string object_filename = make_object_filename (hash);

  struct stat stat, obj_stat, d_stat;
  if (lstat (new_filename.c_str(), &stat) != 0)
    {
      error = "can't lstat filename '" + new_filename + "'";
      return false;
    }

  if (!gf.parse (git_filename))
    {
      error = "can't parse git file '" + git_filename + "'";
      return false;
    }

  gf.hash = hash;
  gf.size = stat.st_size;
  gf.mtime = stat.st_mtim.tv_sec;
  gf.mtime_ns = stat.st_mtim.tv_nsec;

  if (!gf.save (git_filename))
    {
      error = "can't save git file '" + git_filename + "'";
      return false;
    }

  if (lstat (object_filename.c_str(), &obj_stat) == 0)    // hash is already known
    {
      if (unlink (new_filename.c_str()) != 0)
        {
          error = "can't remove file '" + new_filename + "'";
          return false;
        }
      return true;
    }

  string dirname = get_dirname (object_filename);
  if (lstat (dirname.c_str(), &d_stat) != 0)
    {
      // need to create hash directory
      if (mkdir (dirname.c_str(), 0755) != 0)
        {
          error = "can't create directory '" + dirname + "'";
          return false;
        }
    }

  // move file to hash repo
  if (rename (new_filename.c_str(), object_filename.c_str()) != 0)
    {
      error = "can't rename file '" + new_filename + "' to '" + object_filename + "'";
      return false;
    }

  // set mode to -r--------
  if (chmod (object_filename.c_str(), 0400) != 0)
    {
      error = "can't chmod 0400 file '" + object_filename + "'";
      return false;
    }
  return true;
}
