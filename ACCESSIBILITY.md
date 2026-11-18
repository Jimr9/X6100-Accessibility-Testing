# Accessibility fork

Experimental branch adding spoken feedback (screen-reader style) to the X6100 GUI, for use with the radio's own text-to-speech. Built and tested by a blind ham using JAWS conventions as a guide, not a sighted assumption of what "should" be spoken.

## How to try it

Flash the image from [Releases](https://github.com/Jimr9/X6100-Accessibility-Testing/releases) the same way as upstream (see main README). Turn Voice on in Settings.

## One thing worth knowing before you start

A few actions - starting a **recording**, **sending** a saved message, **beaconing**, **playing back** a recording, and **Bluetooth headset PTT** - use a two-step "arm" pattern instead of doing the thing on the first press:

1. First press: announces what it's about to do ("Ready to record, press again to start") and waits.
2. Second press: does it, silently.

This is deliberate, not a bug - speaking at the exact instant those actions start was found to interfere with the radio's own audio hardware. If you press once and nothing seems to happen yet, that's normal - press again.

Menus and settings are otherwise navigated with the same knob used for everything else; turning it while a list has only one entry (or none) still reads that entry.

## New apps from upstream v1.2.0: JS8, WeFax, SSTV, Channel List

JS8, WeFax and SSTV (APP pages 3 and 4) came from upstream with no speech of their own. Rather than wiring each of their many screens one by one, a general "app speech helper" is switched on while one of these windows is open (after its name is announced) and off again when it closes:

- **F-key pages** are announced by name ("JS8 page 2", "WeFax page 3").
- **F-key presses** read the button's label as it now reads, so a toggle tells you its new state ("RX Mode Autodetect"). If the button pops up a message instead, the message is read.
- **Pop-up messages** at the bottom of the screen are read as they appear ("Fax saved", "SSTV mode detected: Martin 1", "Set your callsign first", ...).
- **Turning the knob** reads whatever it lands on: a list entry with its position ("Inbox, 3 of 7"), a text field ("Edit text" and its contents or hint), a switch, a dropdown.
- **Nothing is spoken while transmitting**, and any announcement still playing is cut off the moment the app starts a digital transmission (JS8, SSTV, FT8), so speech can't go out on the air or fight the transmission for the audio path. JS8 transmits on its own (heartbeats, auto replies): you'll hear nothing during those overs.

Also specific to each app:

- **JS8**: turning the knob through the received-message list reads each line (new lines arriving don't interrupt). A message addressed to you is read out as it arrives ("Message for you: ..."); alert words and new stations are read via their pop-ups.
- **WeFax**: the station list, its schedule and the Fax Browser read each entry as you turn.
- **SSTV**: the received-pictures browser and the transmit picture/template lists read each entry. On the transmit screen each step's instruction is read ("Choose an image..."), and each transmit type is read with its help line.
- **Channel List** (APP page 4, a long-press key, or the hand mic's V/M key): opening says "Channel list" and the current channel; turning reads each channel's name, frequency, mode and position ("BBC, 9.41 megahertz, A M, 3 of 20"). In Drag mode it reads the new position.
- **Callsign** (APP page 2): upstream redesigned it - type the bare call, press Enter, then pick no suffix, /P or /QRP from a list. The list reads its title and the highlighted choice as you turn; pressing saves and says "Callsign" and the full call. Clearing the call or a too-long call are announced.
- **Settings > Interface > S-meter offset** (new) is announced like the other sliders.

These four apps are the least-tested part of this build: their speech comes mostly from the general helper above, so some screens may read awkwardly (symbols, abbreviations) or say too much or too little. Reports of exactly what you heard, and where, are the most useful feedback.

## Known limitations

- **SWR Scan and ATU tuning**: starting/stopping an SWR scan, or hearing the result after an auto-tune, can occasionally wedge the speech engine entirely (radio keeps working, RX audio is fine, but no more speech until a soft reset - hold LOCK; SWR Scan's version has needed a full power cycle, ATU's has recovered with just the LOCK-hold). Confirmed as of 2026-08-26 this is a BASE firmware issue, not a bug in this fork or the GUI app: per gdyuldin, BASE mixes several audio sources (handset mic, internal mic, USB soundcard, and the Linux/GUI side) into one output, and after ATU (and possibly SWR Scan) runs, BASE stops feeding the Linux-side input into that mix - so nothing the app plays can be heard, even though PulseAudio/ALSA both report a fully healthy stream the whole time. No GUI-side fix eliminates the wedge itself; gdyuldin has a patched BASE firmware that aims to fix this, at least for the ATU case; the latest public BASE patch release (r13, 2026-09-30) does not mention it, so treat it as still unfixed. Both the ATU key and the SWR Scan Run button use a press-once-to-arm, press-again-to-act pattern (results, when there are any to report, are read out on the next idle press after stopping) - this doesn't fix the underlying wedge, but keeps the hazardous hardware toggle and the announcement further apart, which live testing suggests is somewhat more reliable in practice. Avoid relying on SWR Scan or ATU auto-tune with voice on for now.
- **Bluetooth window** (APP menu, new upstream in this build): opens with a spoken summary; the Status button (page 3) repeats it. Turning the knob reads each device with its position ("2 of 5"). Connections, scan results, pairing codes (digit by digit) and failures are spoken as they happen. Nothing is spoken while the headset PTT is transmitting - with listening off, speech would go out on the air - so "PTT off, receiving" is the first thing you hear after an over. When Bluetooth listening is on, speech goes to the Bluetooth headphones instead of the radio's speaker.
- **FT8 / band-switching deadlock**: RESOLVED as of upstream v0.34.2 (gdyuldin's parameter/Subject redesign, see [issue #261](https://github.com/gdyuldin/x6100_gui/issues/261)). No longer an issue on current builds.

- **GPS window**: still has no speech beyond its name (unchanged from earlier builds).
- **Speech is silent during a transmission** in JS8/WeFax/SSTV (see above); a pop-up shown while transmitting (for example JS8's high-SWR safety cut-off) is not read afterwards.

Builds through the upstream 1.0.x merge (Bluetooth, new Settings layout) were live-tested on real hardware. This build is merged onto upstream v1.2.0 and adds the speech above; it has not been live-tested yet. Feedback welcome.
