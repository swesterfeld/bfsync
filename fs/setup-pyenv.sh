export PYTHONPATH=$PWD/`python setup-pyenv-helper.py`:$PYTHONPATH
export LD_LIBRARY_PATH=$PWD/.libs:$LD_LIBRARY_PATH
export PATH=$PATH:$PWD/../src
export PS1="[py$(echo "$PWD $(hostname)" | sha1sum | sed 's/^\(..\).*/\1/g')] $PS1"
