#!/bin/zsh
# Runs one of the sound tools with its Python packages, keeping every cache and temporary file on the external drive,
# at low priority and on two threads so the Mac stays usable.
cd "$(dirname "$0")"
SCRATCH=/Volumes/External/code/tests/outerwilds/.scratch
mkdir -p "$SCRATCH/tmp"
export UV_CACHE_DIR="$SCRATCH/uv-cache" TMPDIR="$SCRATCH/tmp"
export OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=2 VECLIB_MAXIMUM_THREADS=2
exec nice -n 10 uv run --no-project --with numpy --with scipy --with soundfile --with av python -B "$@"
