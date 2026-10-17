# Dynamic performance results

`tools/run-performance-benchmark.sh` creates timestamped subdirectories here.
Each run contains CSV, JSON, and Markdown summaries for deterministic offline
audio callbacks at 44.1 and 48 kHz. These results are host-specific: use the
Raspberry Pi run as the optimisation baseline.

For GUI comparisons, launch the standalone under Cage with
`MYK_TRACKER_UI_FPS=10`, `25`, or `40`, use the local MCP controls to switch
pages and machine states, and record process CPU with the host's monitoring
tool. The normal tracker update rate remains 25 FPS when this variable is not
set.
