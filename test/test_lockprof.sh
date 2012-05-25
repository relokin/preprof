#!/bin/bash

export LD_PRELOAD="../liblockprof.so"
export PREPROF_FILE="test_lockprof.log"

./test_lockprof 4

