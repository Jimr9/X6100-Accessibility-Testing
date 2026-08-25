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

- **SWR Scan and ATU tuning**: starting/stopping an SWR scan, or hearing the result after an auto-tune, can occasionally wedge the speech engine entirely (radio keeps working, RX audio is fine, but no more speech until a soft reset - hold LOCK; SWR Scan's version has needed a full power cycle, ATU's has recovered with just the LOCK-hold). Root cause not yet found, but as of 2026-08-25 it's confirmed *not* an accessibility-code bug: live gdb tracing shows the announcement call happens completely normally (right text, nothing blocking it) right before the wedge, so whatever's happening is downstream in the actual audio path, likely triggered by the RF hardware mode toggle itself rather than anything about the speech timing. The two look like the same underlying issue reached via two different hardware-control paths. Avoid relying on SWR Scan or ATU auto-tune with voice on for now.
- **FT8 / band-switching deadlock**: RESOLVED as of upstream v0.34.2 (gdyuldin's parameter/Subject redesign, see [issue #261](https://github.com/gdyuldin/x6100_gui/issues/261)). No longer an issue on current builds.

Everything else has been live-tested on real hardware. Feedback welcome.
