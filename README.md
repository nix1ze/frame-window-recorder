# Frame Window Recorder

A Windows Geode mod for Geometry Dash that records a successful run as a macro and then probes each recorded input earlier and later to estimate its frame window.

## Current v0.1 behavior

- Records press/release inputs while playing normally.
- Saves the successful run when the level is completed.
- Adds **Analyze** and **Replay** buttons to the pause menu.
- During analysis, replays the run while shifting one input at a time.
- Counts successful early/late shifts and reports a total frame window.
- Can analyze releases as well as presses.
- Saves `last_macro.csv` and `frame_windows.csv` in the mod config directory.

## Install

Download the Windows `.geode` artifact from GitHub Actions, extract the artifact ZIP, then place the `.geode` file into:

`Geometry Dash/geode/mods`

Restart Geometry Dash.

## Notes

This is an experimental first build. The analyzer currently replays full runs for probes, so long levels can take a while to scan.
