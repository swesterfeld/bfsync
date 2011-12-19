#!/usr/bin/env python

from setuptools import setup

setup (
  name = 'bfsync',
  version = '0.2.0',
  description = 'Big File synchronization based on Git',
  author = 'Stefan Westerfeld',
  author_email = 'stefan@space.twc.de',
  url = 'http://space.twc.de/~stefan/bfsync.php',
  package_dir = {
    'bfsync': 'src/bfsync'
  },
  packages = ['bfsync'],
  data_files=[('man/man1', ['bfsync.1'])],
  entry_points = {
    "console_scripts": [ "bfsync = bfsync.main:main" ]
  }
)
