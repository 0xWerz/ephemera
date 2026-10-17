#!/bin/zsh
# Runs one of the terrain tools with its Python packages, keeping every cache on the external drive.
cd "$(dirname "$0")"
export UV_CACHE_DIR=/Volumes/External/code/tests/outerwilds/.scratch/uv-cache
export NUMBA_CACHE_DIR=/Volumes/External/code/tests/outerwilds/.scratch/numba-cache
exec uv run --no-project --with numpy --with numba --with pillow --with scipy python -B "$@"
