# Accessibility fork

Experimental branch adding spoken feedback (screen-reader style) to the X6100 GUI, for use with the radio's own text-to-speech. Built and tested by a blind ham using JAWS conventions as a guide, not a sighted assumption of what "should" be spoken.

## How to try it

Flash the image from [Releases](https://github.com/Jimr9/X6100-Accessibility-Testing/releases) the same way as upstream (see main README). Turn Voice on in Settings.

## One thing worth knowing before you start

A few actions - starting a **recording**, **sending** a saved message, **beaconing**, and **playing back** a recording - use a two-step "arm" pattern instead of doing the thing on the first press:

1. First press: announces what it's about to do ("Ready to record, press again to start") and waits.
2. Second press: does it, silently.

This is deliberate, not a bug - speaking at the exact instant those actions start was found to interfere with the radio's own audio hardware. If you press once and nothing seems to happen yet, that's normal - press again.

Menus and settings are otherwise navigated with the same knob used for everything else; turning it while a list has only one entry (or none) still reads that entry.

## Known limitations

- **SWR Scan and ATU tuning**: starting/stopping an SWR scan, or hearing the result after an auto-tune, can occasionally wedge the speech engine entirely (radio keeps working, RX audio is fine, but no more speech until a soft reset - hold LOCK; SWR Scan's version has needed a full power cycle, ATU's has recovered with just the LOCK-hold). Confirmed as of 2026-08-26 this is a BASE firmware issue, not a bug in this fork or the GUI app: per gdyuldin, BASE mixes several audio sources (handset mic, internal mic, USB soundcard, and the Linux/GUI side) into one output, and after ATU (and possibly SWR Scan) runs, BASE stops feeding the Linux-side input into that mix - so nothing the app plays can be heard, even though PulseAudio/ALSA both report a fully healthy stream the whole time. No GUI-side fix eliminates the wedge itself; gdyuldin has a patched BASE firmware that aims to fix this, at least for the ATU case; the latest public BASE patch release (r13, 2026-09-30) does not mention it, so treat it as still unfixed. Both the ATU key and the SWR Scan Run button use a press-once-to-arm, press-again-to-act pattern (results, when there are any to report, are read out on the next idle press after stopping) - this doesn't fix the underlying wedge, but keeps the hazardous hardware toggle and the announcement further apart, which live testing suggests is somewhat more reliable in practice. Avoid relying on SWR Scan or ATU auto-tune with voice on for now.
- **Bluetooth window** (new upstream in this build, APP menu): not yet given spoken feedback - its controls and device list are silent. When Bluetooth listening is on, speech goes to the Bluetooth headphones instead of the radio's speaker.
- **FT8 / band-switching deadlock**: RESOLVED as of upstream v0.34.2 (gdyuldin's parameter/Subject redesign, see [issue #261](https://github.com/gdyuldin/x6100_gui/issues/261)). No longer an issue on current builds.

Builds up to the upstream v0.34.2 merge were live-tested on real hardware. The merge onto upstream 1.0.x (new Settings layout, Bluetooth, FT8 country/distance display) is not live-tested yet. Feedback welcome.
