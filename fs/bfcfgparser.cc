/*
  bfsync: Big File synchronization based on Git

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

#include <assert.h>
#include <stdio.h>
#include "bfsyncfs.hh"
#include "bfcfgparser.hh"

#include <string>
#include <map>
#include <vector>

using std::string;
using std::map;
using std::vector;

namespace BFSync
{

const map<string, vector<string> >&
CfgParser::values()
{
  return m_values;
}

const string&
CfgParser::error()
{
  return m_error;
}

static bool
id_char (char c)
{
  return ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == ':'  || c == '.' || c == '@' ||
           c == '/' || c == '-');
}

Token
CfgParser::get_token()
{
  token_value = "";

  int c = fgetc (cfg_file);

  if (c == EOF)
    return TOKEN_EOF;

  if (id_char (c))
    {
      token_value += c;
      while (1)
        {
          c = fgetc (cfg_file);
          if (id_char (c))
            {
              token_value += c;
            }
          else
            {
              ungetc (c, cfg_file);
              return TOKEN_IDENTIFIER;
            }
        }
    }
  if (c == ' ' || c == '\t')    /* skip whitespace */
    return get_token();
  if (c == '\n')
    {
      line++;
      return get_token();
    }
  if (c == ';')
    return TOKEN_SEMICOLON;
  if (c == '{')
    return TOKEN_OPEN;
  if (c == '}')
    return TOKEN_CLOSE;
  if (c == '"')
    {
      while (1)
        {
          c = fgetc (cfg_file);
          if (c == '"')
            {
              return TOKEN_STRING;
            }
          else if (c == '\\')
            {
              c = fgetc (cfg_file);
              token_value += c;
            }
          else
            {
              token_value += c;
            }
        }
    }
  if (c == '#')    // strip comments
    {
      while (1)
        {
          c = fgetc (cfg_file);
          if (c == '\n')
            {
              line++;
              return get_token();
            }
          if (c == EOF)
            return TOKEN_EOF;
        }
    }

  return TOKEN_ERROR;
}

void
CfgParser::print_token (Token t)
{
  switch (t)
    {
      case TOKEN_ERROR:   printf ("TOKEN_ERROR\n");
                          break;
      case TOKEN_IDENTIFIER:  printf ("TOKEN_IDENTIFIER(%s)\n", token_value.c_str());
                          break;
      case TOKEN_STRING:  printf ("TOKEN_STRING(%s)\n", token_value.c_str());
                          break;
      case TOKEN_SEMICOLON:  printf ("TOKEN_SEMICOLON(;)\n");
                          break;
      case TOKEN_OPEN:    printf ("TOKEN_OPEN({)\n");
                          break;
      case TOKEN_CLOSE:   printf ("TOKEN_CLOSE(})\n");
                          break;
      case TOKEN_EOF:     printf ("TOKEN_EOF\n");
                          break;
    }
}

bool
CfgParser::parse_error (const string& error)
{
  m_error = string_printf ("%s:%d: %s", filename.c_str(), line, error.c_str());
  return false;
}

bool
CfgParser::parse (const string& filename)
{
  cfg_file = fopen (filename.c_str(), "r");
  if (!cfg_file)
    {
      m_error = "config file '" + filename + "' not found (or not readable)";
      return false;
    }

  this->filename = filename;
  line = 1;

  bool success;
  while (1)
    {
      Token t = get_token();
      // print_token (t);
      if (t == TOKEN_IDENTIFIER)
        {
          if (!parse_group_or_key_value (token_value))
            {
              success = false;
              break;
            }
        }
      if (t == TOKEN_EOF)
        {
          success = true;
          break;
        }
      if (t == TOKEN_ERROR)
        {
          success = parse_error ("expected identifier or EOF");
          break;
        }
    }
  fclose (cfg_file);
  return success;
}

bool
CfgParser::parse_group (string name)
{
  Token t = get_token();
  if (t == TOKEN_IDENTIFIER)
    {
      if (!parse_group_or_key_value (name + "/" + token_value))
        return false;

      return parse_group (name);
    }
  else if (t == TOKEN_CLOSE)
    {
      return true;
    }
  else
    {
      return parse_error (string_printf ("group '%s', expected identifier or '}'", name.c_str()));
    }
}

bool
CfgParser::parse_group_or_key_value (string name)
{
  Token t = get_token();
  if (t == TOKEN_OPEN)
    {
      return parse_group (name);
    }
  if (t == TOKEN_IDENTIFIER || t == TOKEN_STRING)
    {
      vector<string>& values = m_values[name];
      values.push_back (token_value);
      while (1)
        {
          Token t = get_token();
          if (t == TOKEN_SEMICOLON)
            return true;
          if (t == TOKEN_IDENTIFIER || t == TOKEN_STRING)
            values.push_back (token_value);
          else
            return parse_error (string_printf ("key '%s', expected string/identifier or ';'", name.c_str()));
        }
    }
  else
    return parse_error (string_printf ("key/group '%s', expected string/identifier or ';'", name.c_str()));
}

}

#if 0
int
main (int argc, char **argv)
{
  assert (argc == 2);

  BFSync::CfgParser cfg_parser;

  if (cfg_parser.parse (argv[1]))
    {
      printf ("parse ok\n");
      map<string, vector<string> > values = cfg_parser.values();
      for (map<string, vector<string> >::iterator vi = values.begin(); vi != values.end(); vi++)
        {
          printf ("KEY %s:\n", vi->first.c_str());
          vector<string> sqlite_sync = vi->second;
          for (vector<string>::iterator si = sqlite_sync.begin(); si != sqlite_sync.end(); si++)
            {
              printf (" - %s\n", si->c_str());
            }
        }
    }
  else
    {
      printf ("ERROR: %s\n", cfg_parser.error().c_str());
    }
}
#endif
