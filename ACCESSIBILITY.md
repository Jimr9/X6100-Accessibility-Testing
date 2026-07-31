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

- **SWR Scan**: starting or stopping a scan can wedge the speech engine entirely (radio keeps working, RX audio is fine, but no more speech until a full power cycle). Root cause not yet found - it's not about what gets announced, since it still happens with zero announcements anywhere near the scan. Avoid relying on SWR Scan with voice on for now.
- **FT8 / band-switching**: tuning across a band boundary (not just within FT8) can deadlock the whole application (frozen, but process alive). Reported upstream as [issue #262](https://github.com/gdyuldin/x6100_gui/issues/262). Unrelated to voice/accessibility - reproducible on plain upstream too.

Everything else has been live-tested on real hardware. Feedback welcome.
