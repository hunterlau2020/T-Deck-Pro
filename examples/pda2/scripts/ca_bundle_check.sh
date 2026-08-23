#!/bin/sh
# CA bundle sanity check - thin wrapper around the Python implementation
# (the extraction needs byte-exact newline handling that is not portable
# across shells; see ca_bundle_check.py).
#
# Usage: sh ca_bundle_check.sh
exec python "$(dirname "$0")/ca_bundle_check.py" "$@"
