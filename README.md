# Pocket Step for Flipper Zero

A compact rhythm game external app for Flipper Zero.

## Included
- 4-lane rhythm gameplay mapped to **Left / Down / Up / Right**
- Two built-in demo songs
- Internal speaker playback synced to the step chart
- Score, combo, and end-of-song results

## Folder placement
Copy this folder into:

`applications_user/pocket_step`

inside your Flipper Zero firmware tree.

## Build
From the firmware root:

```bash
./fbt fap_pocket_step
```

or:

```bash
./fbt build APPSRC=applications_user/pocket_step
```

## Notes
- The game uses Flipper's internal speaker, so if another app already owns the speaker, the game will still run in muted mode.
- To add more songs, duplicate one of the `StepNote` and `MelodyNote` arrays and add a new entry to the `songs[]` table.
- Song timing is expressed in **milliseconds**.
