# LION KIDS user manual

## Start playing

Unzip the browser release and serve its `lion-kids-web` folder using a local web server. With Python 3 installed, open Terminal or PowerShell, switch to the extracted `lion-kids-web` folder, and run:

```sh
python3 -m http.server 8000 --bind 127.0.0.1
```

On Windows, use `py -3` if `python3` is unavailable. Open [127.0.0.1:8000](http://127.0.0.1:8000) and click **LION KIDS starten**. If you already have songs in the emulator, keep using the same browser profile, address and port. Your three song slots are saved in that browser. A different address, port or profile has separate storage. Clearing site data removes that browser's songs.

Turn **SELECT** to pick one of eight tracks, **PRESETS** to choose its category, and **ALGORITHM** to pick a sound. These knobs stop at either end. The track icon shows the category, and the counter shows the preset's position. On a new song the tracks are Acid Line, Marimba, Twill, Bellows, Dusty Pad, Breath Lead, Flute and Whisper Pad. Keys and Plucks start with reverb and delay.

- On **HOME**, K1 changes track volume, K2 changes the loop length (16, 32 or 64 steps), K3 chooses a chord mode, and K4 chooses a scale.
- **PLAY** starts or stops playback. In HOME and Performance, press **REC** while playing to overdub the selected track. If stopped, press REC and then PLAY. Press REC again to finish the take.
- **OCT−/+** changes octave. Turn **SELECT** to move to another track.

## Edit the loop

**SEQ** first opens **Performance**, where the keyboard plays live. Press SEQ again to switch to **Steps**. Further presses alternate views. After visiting another screen, SEQ reopens the view you last used.

| Control | Performance | Steps |
| --- | --- | --- |
| SELECT | Choose a track | Choose a track |
| K4 | Move the cursor on that track | Move the cursor on that track |
| K1 | Set the selected note's velocity | Set the selected note's velocity, or set velocity before adding it |
| K2 | Transpose the selected melodic note or chord | Transpose the selected note or chord. On an empty step, set the root for new notes |
| K3 | Set the selected step's length | Set the selected step's length |
| Keys | Play live | White keys toggle steps on the visible page. Black keys choose a hit on drum tracks |
| EDIT | Open Quick FX | Add or remove the note or selected drum hit at the cursor |

K4 moves between pages and wraps at the ends of the current track. For example, turning left from step 1 of a 32-step track selects step 32. Each track has its own cursor. In Performance the rows follow their own loops while playing. Turning K4 lets you inspect the current track independently until you start or stop playback or change views. On a drum step, velocity can be changed for the selected hit. Length applies to the step's group of hits. **Auto** uses the pattern's default note length.

![Performance view](img/lion-kids-performance.png)

![Steps view](img/lion-kids-steps.png)

## Add effects

| Button | Panel | K1 / K2 / K3 / K4 |
| --- | --- | --- |
| FX | Reverb | Amount / decay / room size / tone |
| SEL | Delay | Amount / feedback / beat division / tone |
| EDIT outside Steps and GLO | Quick FX | Low-pass / drive / bitcrush / chorus |
| ENV | Envelope | Attack / hold / sustain / release |
| LFO | Modulation | Depth / target / rate / waveform |
| ARP | Arpeggiator | Order / rate / chord / scale |

A lit effect button means it is enabled on the selected track. Pressing it from another panel opens its controls. Press it again in that panel to switch it off. Reverb and delay amounts are independent per track. Their other settings are shared within the song.

## Save, open or clear a song

Press **GLO**. K1 sets the tempo, K2 sets the metronome level, K3 sets swing for the selected track, and K4 previews song slots 1–3. Press **EDIT** to open the previewed song. **SAVE** writes to the currently loaded slot. To clear the selected slot, hold **GLO for five seconds**, then confirm with EDIT or cancel with HOME. The confirmation closes after five seconds.

Hold **EDIT** on GLO to open the pixel-name editor. K1 selects the canvas, K2 and K3 move the cursor, and K4 chooses and paints a color. PLAY paints with the current color, SEL resets the canvas, EDIT assigns the picture, and HOME leaves the editor. The templates autosave. A song keeps its own copy of the picture you assign.

Hold **HOME** and turn K1 left to undo or right to redo. The session remembers the last ten actions, including song saves and deletions. Undo history clears when the emulator restarts.

## Computer keyboard

| Shortcut | Control |
| --- | --- |
| Q–I, A–K | White keys |
| 1–0, minus | Black keys |
| Up / Down away from a focused knob | SELECT |
| Tab, then arrow keys | Focus and turn a knob |
| Space / Return from the panel | PLAY / REC |
| / | SEQ |
| O / N / V | GLO / EDIT / SAVE |
| Esc | HOME |
| Z / X | OCT− / OCT+ |

You can also click the panel buttons and drag or scroll over the knobs. Tab moves focus between controls. Space or Return presses a focused panel button.
