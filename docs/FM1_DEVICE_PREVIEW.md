# LION KIDS FM‑1 device preview

This candidate uses the normal M‑VAVE firmware update path and keeps the stock bootloader. It includes the built-in instruments and presets, eight tracks, the Kids screens and three song slots. It uses the former imported-sample window for Kids songs. Imported samples, Bluetooth, USB audio, USB MIDI host, and screen mirroring are excluded from this profile. The separate browser emulator remains available and keeps its browser songs.

The device has one 64-step pattern per track, with the same 16, 32 and 64-step lengths in the Kids controls. Ordinary edits have Undo and Redo. Save, open and clear are outside the device edit history. Each saved song can use up to 16,364 bytes in flash. There is a second flash copy for an interrupted save. Device songs and browser songs are kept separately.

Preview 0.0.2 keeps both audio cores working while LFO and Quick Effects are active. It restores parallel processing of reverb, delay and voices. The LFO phase moves in a small strip below the waveform.

Preview 0.0.1 includes the factory recorded instruments and break loops in the firmware image. It also speeds up continuous controls on the panel and repaints the full screen when returning from a graph view. Preset and step selectors still move one item per detent.

## Install from Chrome or Edge

1. Open the [LION KIDS installer](https://vmphilemon-glitch.github.io/fm1-loom-kids/). Check that it says **LION KIDS device preview**.
2. Back up anything you want to keep using your current firmware's backup tool. Connect the FM‑1 directly with a USB data cable.
3. Click **Install**, allow MIDI access when asked, and follow the page. If the installer asks for update mode, hold OCT− and OCT+ together for five seconds. Keep the FM‑1 connected until the installer reports completion and it has restarted.
4. Verify HOME, the keyboard, playback and SAVE. Use an empty slot to check that a small saved pattern reopens after a power cycle. The older Puya revision still needs that first physical test.

The installer also has **Back to the stock firmware**.

## Download the installer bundle

The **LION KIDS FM-1 preview** artifact contains the firmware, installer and corresponding source. To serve it locally, unzip it and run `python3 -m http.server 8010 --bind 127.0.0.1` from the folder with `index.html`. Open [http://127.0.0.1:8010](http://127.0.0.1:8010) in Chrome or Edge. [Preview build workflow](https://github.com/vmphilemon-glitch/fm1-loom-kids/actions/workflows/lion-device.yml)
