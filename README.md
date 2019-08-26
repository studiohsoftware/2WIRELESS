# 2WIRELESS
# Wireless Preset Manager
## Supported REST Calls (so far)
- ### http://192.168.0.1/remoteenable
Global remote enable.
- ### http://192.168.0.1/remotedisable
Global remote disable.
- ### http://192.168.0.1/savepreset?preset=(0-29)
Save current settings to specified preset.
Example: http://192.168.0.1/savepreset?preset=0
- ### http://192.168.0.1/recallpreset?preset=(0-29)
Recall specified preset.
Example: http://192.168.0.1/recallpreset?preset=29
- ### http://192.168.0.1/getpresets?addr=(module addr in hex)
Retrieve all 30 presets from the specified module.
Example (259A): http://192.168.0.1/getpresets?addr=0x28
- ### http://192.168.0.1/midinoteon?mask=(0x0-0xF)&note=(0-127)&velo=(0-255)
Send MIDI note ON message. 
Example (bus A, note G1, half velocity): http://192.168.0.1/midinoteon?mask=0x8&note=31&velo=127
- ### http://192.168.0.1/midinoteoff?mask=(0x0-0xF)&note=(0-127)&velo=(0-255)
Send MIDI note OFF message.
Example (All buses, note F3, max velocity) http://192.168.0.1/midinoteoff?mask=0xF&note=63&velo=255
- ### http://192.168.0.1/midifinetune?mask=(0x0-0xF)&tune=(0x00-0x3F)
Send MIDI fine tune message. 
Example (Bus B, lowest fine tune -49): http://192.168.0.1/midifinetune?mask=04&tune=0x00
Example (Bus C, Ft=An): http://192.168.0.1/midifinetune?mask=02&tune=0x32
Example (Bus D, highest fine tune +49): http://192.168.0.1/midifinetune?mask=01&tune=0x63
- ### http://192.168.0.1/midibend?mask=(0x0-0xF)&bend_lsb=(0x0-0x7F)&bend_msb=0x0-0x7F)
Send MIDI bend command.
Example (Bus A, lowest bend): http://192.168.0.1/midibend?mask=0x8&bend_lsb=0x00&bend_msb=0x00
Example (Bus A, no bend): http://192.168.0.1/midibend?mask=0x8&bend_lsb=0x00&bend_msb=0x40
Example (Bus A, highest bend): http://192.168.0.1/midibend?mask=0x8&bend_lsb=0x7F&bend_msb=0x7F
- ### http://192.168.0.1/midiclockstart
Midi clock start.
- ### http://192.168.0.1/midiclockstop
Midi clock stop
- ### http://192.168.0.1/midiclock
Midi clock. 24 per beat required?
