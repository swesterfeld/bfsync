PYTHON_FILES = bfsync.py merge-test.py bfapply.py bfview.py xzperf.py socktest.py slowwrite.py \
               testdbsize.py bfdiff.py fstest.py xmount.py bfpview.py cmdtest.py foreach_test.py \
               h2f_insert.py mumon.py sqlexpand.py testhcdict.py walk_test.py

SH_FILES = mkfiles.sh multi-run-test.sh writetest.sh create-sparse-history-250.sh create-sparse-history.sh \
           link-del-test.sh mklinks.sh mknfiles.sh mktouch.sh mrt-join-dedup.sh mrt-join.sh MRTNC.sh \
           multi-add-test.sh multi-get-test.sh

SUBDIRS = bfsync

EXTRA_DIST = $(PYTHON_FILES) $(SH_FILES)

check: python-files-test shell-files-test

python-files-test:
	ls *.py | sort -u > PFT-1
	ls $(PYTHON_FILES) | sort -u > PFT-2
	cmp PFT-1 PFT-2 || exit 1
	rm PFT-1 PFT-2

shell-files-test:
	ls *.sh | sort -u > SFT-1
	ls $(SH_FILES) | sort -u > SFT-2
	cmp SFT-1 SFT-2 || exit 1
	rm SFT-1 SFT-2
