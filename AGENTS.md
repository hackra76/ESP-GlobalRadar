# ESP-MeteoRadar Project Rules & Agent Workflow

## Autonomy & Decision Making
- **Full Autonomous Mode (Zero-Question Execution)**: Always act fully autonomously. Never ask for confirmation, interactive approval, or ask questions like "Chcete to zapracovať?" / "Mám urobiť túto zmenu?".
- **Auto-Accept & Direct Applied Changes**: All code changes, edits, and file modifications MUST be directly applied and accepted automatically without requiring manual per-change user approval or prompting.
- **Direct Implementation on Inquiries**: Whenever the user asks if something is possible, how to fix an issue, or asks about a new feature/tweak, immediately and proactively implement the complete solution, apply file edits, verify compilation, and present the completed working result.
- **Proactive Execution**: Directly implement solutions, apply edits, and verify results without unnecessary round-trips or asking permission to proceed with obvious next steps.
- **Verification**: Automatically verify code changes by running the appropriate build (`C:\Users\radod\.platformio\penv\Scripts\platformio.exe run` or `pio run`) after making modifications.
- **Problem Solving**: If an error occurs or a build fails, inspect the error output, diagnose the root cause, and autonomously apply the fix and re-verify.

## Directory Structure & Clean Root Policy
- **Root Configuration Files**: Only standard tool configurations belong in root: `platformio.ini`, `partitions.csv`, `README.md`, `LICENSE`, `GEMINI.md`, `AGENTS.md`.
- **Clean Root Directory**: NEVER place ad-hoc helper files, temporary logs, scratch scripts, or redundant assets into the repository root.
- **Appropriate Subdirectories**: Always organize files into their dedicated folders:
  - `scripts/` – Build scripts, python utilities, post-processing tools
  - `include/` – Header files and configurations
  - `src/` – Core implementation C++ files
  - `data/` – Static assets, images, SPIFFS files
  - `.agents/` – Agent workflow rules and instructions
  - `.vscode/` – IDE configurations and recommended extensions

## Project Context
- **Project**: ESP-MeteoRadar (ESP32-C3 Super Mini + GC9A01 240x240 round display)
- **Framework**: Arduino / PlatformIO
- **Build tool**: PlatformIO CLI (`pio run` / PlatformIO penv)
