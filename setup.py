#!/usr/bin/env python

from distutils.core import setup

setup (
  name = 'bfsync',
  version = '0.1.0',
  description = 'Big File synchronization based on Git',
  author = 'Stefan Westerfeld',
  author_email = 'stefan@space.twc.de',
  url = 'http://space.twc.de/~stefan/bfsync.php',
  scripts = ['bfsync'],
  data_files=[('man/man1', ['bfsync.1'])]
)
