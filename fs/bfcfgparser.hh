/*
  bfsync: Big File synchronization tool

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

#ifndef BFSYNC_CFG_PARSER_HH
#define BFSYNC_CFG_PARSER_HH

#include <string>
#include <map>
#include <vector>

namespace BFSync
{

enum Token
{
  TOKEN_ERROR = 0,
  TOKEN_IDENTIFIER = 1,
  TOKEN_SEMICOLON,
  TOKEN_STRING,
  TOKEN_OPEN,
  TOKEN_CLOSE,
  TOKEN_EOF
};

class CfgParser
{
  FILE       *cfg_file;
  std::string token_value;
  int         line;
  std::string filename;
  std::string m_error;

  std::map<std::string, std::vector<std::string> > m_values;

  Token get_token();
  void  print_token (Token t);

  bool parse_group_or_key_value (std::string name);
  bool parse_group (std::string name);
  bool parse_error (const std::string& error);
public:
  bool parse (const std::string& filename);
  const std::map<std::string, std::vector<std::string> >& values();
  const std::string& error();
};

}

#endif
