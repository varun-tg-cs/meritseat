#!/bin/sh
set -eu
DATA_DIR="${DATA_DIR:-/tmp/meritseat}"
mkdir -p "$DATA_DIR"
exec ./admission --host 0.0.0.0 --port "${PORT:-10000}" --data "$DATA_DIR/admissions.tsv"
