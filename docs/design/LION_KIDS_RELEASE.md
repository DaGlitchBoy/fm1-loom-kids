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

Each track icon follows its category. PRESETS changes categories and ALGORITHM moves through that category. The counter beside the sound name shows the current position and total, such as **5/17**. Entering Plucks turns on delay and reverb. You can adjust both while playing. Leaving Plucks switches both off on that track. Your saved songs retain their sounds, switches and pixel names. The new smiley is the starter image for slot 2.

![LION KIDS home screen with the eight starter sounds and preset counter](../img/lion-kids-home.png)

## Hardware review

The inherited platform recognizes two 1 MiB flash parts. The stock Puya P25Q80 reports JEDEC ID `856014`. Later units use the UC25HQ80, ID `B36014`. The host test accepts both and rejects unknown parts, for which the platform uses RAM-only operation. The bootloader, flash map, package identity and platform submodule revisions are unchanged.

The existing hardware target builds LOOM-8 Studio and requires the JieLi toolchain and SDK. The LION KIDS interface and its song storage are built for the host and web emulators. This release candidate has no LION KIDS firmware package or physical boot validation. The inherited firmware workflow runs on the upstream repository only.

[Mac emulator instructions](DELIVERY_8.md)
