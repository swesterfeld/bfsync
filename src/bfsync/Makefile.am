PYTHON_FILES = applyutils.py CfgParser.py commitutils.py diffutils.py HashCache.py \
               __init__.py main.py RemoteRepo.py remoteutils.py ServerConn.py StatusLine.py \
               TransferList.py transferutils.py utils.py xzutils.py journal.py gcutils.py \
               expire.py transferbench.py sqlexport.py textdiff.py

EXTRA_DIST = $(PYTHON_FILES)

check: python-files-test

python-files-test:
	ls *.py | sort -u > PFT-1
	ls $(PYTHON_FILES) | sort -u > PFT-2
	cmp PFT-1 PFT-2 || exit 1
	rm PFT-1 PFT-2
