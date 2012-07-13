#!/usr/bin/env python

from setuptools import setup

setup (
  name = 'bfsync',
  version = '0.3.2',
  description = 'Big File synchronization tool',
  author = 'Stefan Westerfeld',
  author_email = 'stefan@space.twc.de',
  url = 'http://space.twc.de/~stefan/bfsync.php',
  package_dir = {
    'bfsync': 'src/bfsync'
  },
  packages = ['bfsync'],
  entry_points = {
    "console_scripts": [ "bfsync = bfsync.main:main" ]
  }
)
