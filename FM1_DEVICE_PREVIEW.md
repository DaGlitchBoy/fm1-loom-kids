# LION KIDS FM‑1 device preview

This candidate uses the normal M‑VAVE firmware update path and keeps the stock bootloader. It includes the built-in instruments and presets, eight tracks, the Kids screens and three song slots. It uses the former imported-sample window for Kids songs. Imported samples, Bluetooth, USB audio playback from the computer, USB MIDI host, and screen mirroring are excluded from this profile. The separate browser emulator remains available and keeps its browser songs.

The device has one 64-step pattern per track, with the same 16, 32 and 64-step lengths in the Kids controls. Ordinary edits have Undo and Redo. Save, open and clear are outside the device edit history. Each saved song can use up to 16,364 bytes in flash. There is a second flash copy for an interrupted save. Device songs and browser songs are kept separately.

Preview 0.0.4 runs Acid Bass on the primary audio core after a reported fault in its soft drive. It keeps playing on one core if the second core fails and bounds the undo journal during live edits.

Preview 0.0.3 adds read-only song export and stereo USB recording.

Preview 0.0.2 keeps both audio cores working while LFO and Quick Effects are active. It restores parallel processing of reverb, delay and voices. The LFO phase moves in a small strip below the waveform.

Preview 0.0.1 includes the factory recorded instruments and break loops in the firmware image. It also speeds up continuous controls on the panel and repaints the full screen when returning from a graph view. Preset and step selectors still move one item per detent.

## Install from Chrome or Edge

1. Open the [LION KIDS installer](https://daglitchboy.github.io/fm1-loom-kids/). Check that it says **LION KIDS device preview**.
2. Back up anything you want to keep using your current firmware's backup tool. Connect the FM‑1 directly with a USB data cable.
3. Click **Install**, allow MIDI access when asked, and follow the page. If the installer asks for update mode, hold OCT− and OCT+ together for five seconds. Keep the FM‑1 connected until the installer reports completion and it has restarted.
4. Verify HOME, the keyboard, playback and SAVE. Use an empty slot to check that a small saved pattern reopens after a power cycle. The older Puya revision still needs that first physical test.

The installer also has **Back to the stock firmware**.

## Export songs and record live

Open **Export saved songs or record live** from the installer in Chrome or Edge. Use preview 0.0.3 or later.

- For saved music, press **SAVE** on the FM‑1 and stop playback. Select **Read songs from FM‑1**. Download all three slots as a song backup, or use **Export WAV** on a filled slot. A WAV includes one complete loop and its effect tails, with the metronome muted. You can reopen the backup on the export page without the FM‑1 to export more WAVs.
- For a live performance, click **Find audio inputs**, allow audio input access, and select the FM‑1. Click **Record live**, play, and click **Stop**. **Download recording** saves the stereo WAV. Use the FM‑1's own speaker or headphones to listen while you record.

[Export and record page](https://daglitchboy.github.io/fm1-loom-kids/export/)

## Download the installer bundle

The **LION KIDS FM-1 preview** artifact contains the firmware, installer and corresponding source. To serve it locally, unzip it and run `python3 -m http.server 8010 --bind 127.0.0.1` from the folder with `index.html`. Open [http://127.0.0.1:8010](http://127.0.0.1:8010) in Chrome or Edge. [Preview build workflow](https://github.com/DaGlitchBoy/fm1-loom-kids/actions/workflows/lion-device.yml)
