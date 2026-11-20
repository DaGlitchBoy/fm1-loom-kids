# LION KIDS FM‑1 device preview

This candidate uses the normal M‑VAVE firmware update path and keeps the stock bootloader. It includes the built-in instruments and presets, eight tracks, the Kids screens and three song slots. It uses the former imported-sample window for Kids songs. Imported samples, Bluetooth, USB audio, USB MIDI host, and screen mirroring are excluded from this profile. The separate browser emulator remains available and keeps its browser songs.

The device has one 64-step pattern per track, with the same 16, 32 and 64-step lengths in the Kids controls. Ordinary edits have Undo and Redo. Save, open and clear are outside the device edit history. Each saved song can use up to 16,364 bytes in flash. There is a second flash copy for an interrupted save. Device songs and browser songs are kept separately.

## Open the installer on a Mac

1. Download the **LION KIDS FM-1 preview** artifact from the successful [Build LION KIDS FM‑1 preview workflow](https://github.com/vmphilemon-glitch/fm1-loom-kids/actions/workflows/lion-device.yml). Unzip it. Use the folder containing `index.html`, `lion-kids.fwsc` and `lion-kids-res.bin`.
2. Open Terminal in that folder and run `python3 -m http.server 8010 --bind 127.0.0.1`.
3. Open [http://127.0.0.1:8010](http://127.0.0.1:8010) in Chrome or Edge. Check that the page says **LION KIDS device preview**.
4. Back up anything you want to keep using your current firmware's backup tool. Connect the FM‑1 directly to the Mac with a USB data cable. Allow MIDI access when the page asks.
5. Click **Install**, then confirm and follow the page's instructions. If the device needs to enter update mode, hold OCT− and OCT+ together for five seconds as directed by the installer. Leave it connected until the installer reports completion and the FM‑1 has restarted.
6. Verify HOME, the keyboard, playback and SAVE. Use an empty slot to check that a small saved pattern reopens after a power cycle. The older Puya revision still needs that first physical test.

The installer has **Back to the stock firmware** for recovery. Keep the device connected while a firmware update is in progress.
