# Pocket Step

Portrait DDR-style rhythm game for Flipper Zero.

## What changed in this version
- Vertical viewport for side-held play
- 3 built-in songs
- Bigger arrow sprites drawn from inline XBM bitmaps
- Simpler external-app structure focused on compile safety
- Internal speaker playback aligned to chart timing when the speaker can be acquired

## Build
Place the `pocket_step` folder inside `applications_user/` in your firmware tree, then run one of these:

- `./fbt build APPSRC=applications_user/pocket_step`
- `./fbt fap_pocket_step`
- `./fbt launch APPSRC=applications_user/pocket_step`

## Controls
- Menu: Up/Down to pick a song, OK to play, Back to quit
- Game: Left / Down / Up / Right to hit notes, Back to return to menu
- Results: OK or Back to return to menu

## Notes
- The game retries speaker ownership each time a song starts. If the speaker is busy, gameplay still works in muted mode.
- This export was syntax-checked locally with stubbed Flipper headers, but not fully built against a real firmware tree in this environment.
