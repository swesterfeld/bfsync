export PYTHONPATH=$PWD/build/lib.linux-x86_64-2.7:$PYTHONPATH
export LD_LIBRARY_PATH=$PWD/.libs:$LD_LIBRARY_PATH
export PATH=$PATH:$PWD/../src
export PS1="[py$(echo "$PWD $(hostname)" | sha1sum | sed 's/^\(..\).*/\1/g')] $PS1"
