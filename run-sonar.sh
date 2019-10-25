#!/usr/bin/env bash

if [ "$(uname)" == "Darwin" ]; then
	./sonarcloud/macOS/build-wrapper-macosx-x86 --out-dir bw-output make #test
	sonar-scanner
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
    ./sonarcloud/linux/build-wrapper-linux-x86-64 --out-dir bw-output make clean
	sonar-scanner
fi