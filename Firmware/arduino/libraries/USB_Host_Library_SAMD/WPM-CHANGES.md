# WPM changes to USB_Host_Library_SAMD

- Upstream: gdsports/USB_Host_Library_SAMD, commit 04991980013e16a0b82d9fafa5eaba73f666be5c (master HEAD on 2026-10-21), library version 2.0.0
- License: GPL v2 (see gpl2.txt)

Added lines (the only changes):

`src/usbh_midi.h`, class `USBH_MIDI` (public), after `GetAddress()`:

```cpp
        uint8_t GetEpAddress() { return epInfo[epDataInIndex].epAddr; }; // WPM
```

This is the host pipe number of the MIDI bulk-IN endpoint: the library uses the device endpoint number as the SAMD21 host pipe index (`Usb.cpp` `UHD_Pipe_Alloc(..., ul_dev_ep, ...)`).

New file `src/delay.h`: shim, because `usbhub.cpp` includes `"delay.h"`, which arduino:samd >= 1.8.10 no longer ships. It only includes `<Arduino.h>`.

Studio H's shipped build used a patched Arduino core hook for attachInterrupt; this is a behavioural reconstruction, not their source.
