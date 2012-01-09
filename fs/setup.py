#!/usr/bin/env python

from setuptools import setup, Extension

bfsyncdb_module = Extension('_bfsyncdb',
                           extra_compile_args = ['-D_FILE_OFFSET_BITS=64'],
                           sources=['bfsyncdb.cc', 'bfsyncdb.i'],
                           library_dirs = ['.libs'],
                           libraries=['bfsync'],
                           include_dirs=['/usr/local/BerkeleyDB.5.3/include',
                                         '/usr/include/glib-2.0', '/usr/lib/x86_64-linux-gnu/glib-2.0/include' ],
                           language='c++',
                           swig_opts=['-c++']
                           )

setup (name = 'bfsyncdb',
       version = '0.1',
       author      = "SWIG Docs",
       description = """Simple swig example from docs""",
       ext_modules = [bfsyncdb_module],
       py_modules = ["bfsyncdb"],
       )
