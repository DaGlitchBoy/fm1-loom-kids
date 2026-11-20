# LION KIDS release review

The software release candidate is the browser emulator. The GitHub Actions workflow builds it from a recursive checkout using public build dependencies and uploads the web directory. No instrument, private sample bank or files from a particular computer are needed. The emulator's IndexedDB contains local songs and canvases. The browser creates that data at runtime.

## Starter sounds and controls

| Track | Preset | Category |
| --- | --- | --- |
| 1 | Acid line | Bass |
| 2 | Marimba | Plucks |
| 3 | Twill | Drums |
| 4 | Bellows | Keys |
| 5 | Dusty pad | Pads |
| 6 | Breath lead | Leads |
| 7 | Flute | Leads |
| 8 | Whisper pad | Pads |

Each track icon follows its category. PRESETS changes categories and ALGORITHM moves through that category. The counter beside the sound name shows the current position and total, such as **5/17**. Keys and Plucks start with delay and reverb. You can adjust both while playing. Selecting a category outside that pair turns both off on the track. Your saved songs retain their sounds, switches and pixel names. The new smiley is the starter image for slot 2.

![LION KIDS home screen with the eight starter sounds and preset counter](../img/lion-kids-home.png)

## Hardware review

The dedicated FM‑1 profile now builds LION KIDS. The first preview uses 394,680 bytes of its 581,564-byte application slot. It leaves 29,260 bytes of device SRAM free after static allocations, including a 40 KiB arena for musical edits and typed patterns. The build reserves 8 KiB of that free SRAM for headroom. Simulated update entry, loader writes, the two flash IDs, and the device profile's save, clear, restart and ordinary edit Undo passed. A dense six-track song also passed the simulated playback and restore checks. Physical boot and audio timing still need to be verified. See the [device preview and installer](../FM1_DEVICE_PREVIEW.md).

The inherited platform recognizes two 1 MiB flash parts. The stock Puya P25Q80 reports JEDEC ID `856014`. Later units use the UC25HQ80, ID `B36014`. The host test accepts both and rejects unknown parts, for which the platform uses RAM-only operation. The bootloader and platform submodule revisions are unchanged. The device preview has its own identity, `FM-1_73600`.

The inherited default hardware target builds LOOM-8 Studio. Using the JieLi toolchain and hash-verified AC79 SDK files, the offline Studio build occupied 581,536 of the 581,564 bytes in the application slot and left 2,096 bytes of SRAM free. Removing an unused scope recorder reduced the image to 581,472 bytes and increased free SRAM to 2,616 bytes. The offline build passed the simulated update-entry and loader tests and the JEDEC allowlist test for both flash parts.

An isolated compile with the Kids interface and audio enabled exceeded SRAM by 98,600 bytes at link time, even with Kids song writes stubbed out. Turning off Bluetooth, USB audio, USB host and screen mirroring reduced that overage to 26,824 bytes. Neither probe produced a firmware package. The device build also lacks the heap API used by the current Kids song and history code. The largest additional allocations are the audio mix and insert buffers and the two undo capture buffers. The LION KIDS interface and song storage ship in the emulators. The inherited device workflow remains scoped to upstream LOOM-8.

A follow-up probe kept the audio path and removed Studio's input and drawing entry points from the device build. This recovered 2,920 bytes of SRAM and about 121 KiB of linked code and read-only data. Drawing the Kids display in strips of up to 64 rows instead of reserving 120 rows recovered another 26,880 bytes. Ten host screenshots matched pixel for pixel across the stage, sequencer, song shelf, naming editor and shaping views. The 20 WAV files produced by those scenarios were byte-identical. These are measurements from separate prototypes. The display change still needs device timing measurements, and the device build still needs allocation and persistent song storage. The resulting static margin of about 3 KiB is insufficient for the emulator's current song codec, which allocates whole-song buffers.

The save encoder now streams segments of the working song into the existing format. It measures compression first, then allocates only the final encoded record. A synthetic six-track, 64-step song with chords, kit hits, both sends and quick effects occupied 5,804 bytes encoded (76,872 bytes decoded). The emulator saved it, reopened it in a new process, verified notes, velocity and sends, and played it for ten seconds while leaving the slot bytes unchanged. The final record and decoder still need RAM in the emulator implementation. The device path needs a direct sink, bounded loading and a device-specific edit history.

The Kids device storage design uses 96 KiB of the contiguous 108 KiB imported-sample window for three songs with two 16 KiB copies apiece. It reserves two more 4 KiB sectors for shelf metadata and leaves 4 KiB unused. The project pool, calibration, factory sound resources and updater are outside this region. A device profile must disable imported-sample attachment and writes before using this journal.

The device streams real song bytes into the journal without allocating a complete encoded record. It reads and validates the song in 4 KiB blocks using existing display scratch space and validates the staged song before changing the live project. The NOR flash tests interrupt each erase and write during a maximum-sized save and verify that the previous song, other slots and metadata remain intact. Each song copy can hold 16,364 bytes of payload. The device profile allocates one pattern per track, which is what the Kids controls expose. Whole-song save, open and clear sit outside device Undo.
Before promoting the preview to a hardware release, verify boot, save, clear, reboot, sustained audio and stock recovery on both hardware revisions. Firmware Hub release metadata should point at that reviewed release.

[Mac emulator instructions](DELIVERY_8.md)
