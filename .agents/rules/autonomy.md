# Autonomy & Clean Project Structure Guidelines

## Autonomous Execution
- Execute necessary edits, file creations, and terminal commands automatically without pausing to ask the user for confirmation.
- When resolving issues or fulfilling requests, complete the entire loop (investigate -> edit -> compile/verify -> report final status).
- Auto-run PlatformIO compilation (`platformio run`) to ensure code integrity after edits.

## Clean Root Policy
- Do not clutter the workspace root with auxiliary, simulation, script, or temporary files.
- Put simulation files into `sim/`, build scripts into `scripts/`, headers into `include/`, source files into `src/`, and static assets into `data/`.
