#!/bin/bash
rm -rf test/objects
fstest.py -r
setupdb.py
sqlite3 test/db 'DELETE FROM inodes; DELETE FROM links'
