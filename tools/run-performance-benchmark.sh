#!/usr/bin/env bash
set -euo pipefail

# Builds and runs the opt-in offline audio benchmark.  This script never
# changes the normal Debug build or tracker defaults.
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root_dir}/build-perf"
output_dir="${1:-${root_dir}/performance-results/$(hostname)-$(date +%Y%m%d-%H%M%S)}"

cmake -S "${root_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMYK_TRACKER_BUILD_PERFORMANCE_BENCHMARK=ON -DBUILD_TESTING=OFF
cmake --build "${build_dir}" --target myk-tracker-performance-benchmark -j2
mkdir -p "${output_dir}"
"${build_dir}/myk-tracker-performance-benchmark" "${output_dir}"

if perf stat -e task-clock,cycles,instructions true >/dev/null 2>&1; then
    perf stat -o "${output_dir}/perf-stat.txt" -e task-clock,cycles,instructions,context-switches \
        "${build_dir}/myk-tracker-performance-benchmark" "${output_dir}/perf-run"
else
    printf '%s\n' 'perf unavailable: use audio-benchmark.{csv,json,md}; check kernel.perf_event_paranoid for optional external profiling.' > "${output_dir}/perf-stat.txt"
fi

printf 'Results written to %s\n' "${output_dir}"
