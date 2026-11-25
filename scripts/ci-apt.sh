#!/bin/sh
# shellcheck disable=SC2086 # $opts is several words on purpose
# apt-get install for CI runners: `sh scripts/ci-apt.sh pkg...`.
# A slow Ubuntu mirror once held a release job for 40 minutes, so: no recommended extras (wine's pulled in
# gstreamer, jack, va drivers), apt's own retries, and an attempt that stalls is cut off and tried again
# (already downloaded packages are kept, so the second attempt only fetches what is missing).
set -e
opts="-o Acquire::Retries=5 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 -o DPkg::Lock::Timeout=300"
for i in 1 2 3; do
  if sudo timeout 300 apt-get $opts update -qq && \
     sudo DEBIAN_FRONTEND=noninteractive timeout 600 apt-get $opts install -y -qq --no-install-recommends "$@"; then
    exit 0
  fi
  echo "::warning::apt-get attempt $i failed or stalled, trying again"
  sleep $((i * 15))
done
echo "::error::apt-get could not install: $*"
exit 1
