#!/usr/bin/env python

from setuptools import setup, Extension
from subprocess import Popen, PIPE

def pkg_config(*packages):
  cmd = ['pkg-config', '--cflags', '--libs'] + list (packages)
  proc = Popen (cmd, stdout=PIPE, stderr=PIPE)
  output, error = proc.stdout.read(), proc.stderr.read()
  if error:
    raise ValueError (error)
  config = { "-I" : [], "-L" : [], "-l" : []}
  for token in output.split():
    flag, value = token[:2], token[2:]
    config[flag].append (value)
  return config

pkg_config_values = pkg_config ("glib-2.0")

bfsyncdb_module = Extension('_bfsyncdb',
                           depends = [ "bfsyncdb.hh", "bfbdb.hh", "bfhistory.hh", "bfdeduptable.hh" ],
                           extra_compile_args = ['-D_FILE_OFFSET_BITS=64'],
                           sources=['bfsyncdb.cc', 'bfsyncdb.i', 'bfsqlexport.cc', 'bfhashcache.cc'],
                           library_dirs = ['.libs'] + pkg_config_values["-L"],
                           libraries=['bfsync'] + pkg_config_values["-l"],
                           include_dirs=['..'] + pkg_config_values["-I"],
                           language='c++',
                           swig_opts=['-c++'],
                           undef_macros=['NDEBUG']
                           )

setup (name = 'bfsyncdb',
       version = '0.3.3',
       author = 'Stefan Westerfeld',
       author_email = 'stefan@space.twc.de',
       description = 'Big File synchronization tool - python binding',
       ext_modules = [bfsyncdb_module],
       py_modules = ["bfsyncdb"],
       )
