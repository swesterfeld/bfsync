#!/bin/bash
for run in $(seq 1 500)
do
  echo "*** RUN $run ***"
  (
    export BFSYNC_NO_HASH_CACHE=1
    mkdir run$run
    cd run$run
    /usr/bin/time -f "$run mkfiles-time %e" mkfiles.sh 
    /usr/bin/time -f "$run commit-time  %e" bfsync2 commit
  ) 2>&1
done

