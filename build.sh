#!/bin/sh

readonly ENVIRONMENTS="dev"
readonly __DIR__=`cd $(dirname -- "${0}"); pwd -P`

if [ `find "${__DIR__}/deps" -d 0 -type d -empty | wc -l` -eq 1 ]; then
    for env in ${ENVIRONMENTS}; do
        MIX_ENV=${env} mix deps.get
    done
fi

for env in ${ENVIRONMENTS}; do
    MIX_ENV=${env} mix compile
done
