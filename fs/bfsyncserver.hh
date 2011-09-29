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

#include <string>
#include <vector>

namespace BFSync
{

class Server
{
  bool        socket_ok;
  int         socket_fd;
  std::string socket_path;
  pthread_t   thread;

public:
  Server();
  ~Server();

  bool init_socket (const std::string& repo_path);
  void start_thread();

  // server thread:
  void run();
  void handle_client (int client_fd);
  bool decode (const std::vector<char>& buffer, std::vector<std::string>& request);
  void encode (const std::vector<std::string>& result, std::vector<char>& buffer);
};

}
