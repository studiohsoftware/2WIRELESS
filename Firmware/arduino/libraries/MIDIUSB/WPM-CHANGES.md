# WPM changes to MIDIUSB

- Upstream: MIDIUSB 1.0.5 (arduino-libraries/MIDIUSB, tag 1.0.5, Library Manager release)
- License: LGPL 2.1 (see upstream headers / README.adoc)

Added lines (the only changes):

`src/MIDIUSB.h`, class `MIDI_`:

```cpp
	void (*_wpmCb)(int ep) = nullptr; // WPM
	...
	/// WPM: callback invoked from read() as cb(MIDI_RX) after a non-empty packet is dequeued
	void attachInterrupt(void (*cb)(int ep)) { _wpmCb = cb; }
```

`src/MIDIUSB.cpp`, in `MIDI_::read()`, inside the `if (buffer->head != buffer->tail)` block after the tail is advanced:

```cpp
		if (_wpmCb) _wpmCb(MIDI_RX); // WPM
```

Studio H's shipped build used a patched Arduino core hook for attachInterrupt; this is a behavioural reconstruction, not their source.
