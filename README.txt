IR MOTE for Flipper Zero
========================

This is a cleaned-up rebuild of XRemote with:
- app rename to IR MOTE
- safer ownership handling for custom views
- fixed context cleanup leaks
- fixed alt-name parsing leak
- visual custom layout editor instead of a plain variable-item list
- improved menu labels and branding
- preserved IR remote file, learn, analyzer, settings, and control pages

Suggested folder layout:
applications_user/ir_mote/

Build commands:
./fbt fap_ir_mote
./fbt build APPSRC=applications_user/ir_mote

Notes:
- The app keeps the original XRemote source file names internally for easier diffing.
- Asset icons are included in assets/ and compiled through application.fam.
- Long Back exits the visual layout editor.
- Short Back toggles PRESS/HOLD layer in the layout editor.
