# 2WIRELESS
# Wireless Preset Manager
## Supported REST Calls (so far)
- ### http://192.168.0.1/remoteenable
Global remote enable.
- ### http://192.168.0.1/remotedisable
Global remote disable.
- ### http://192.168.0.1/savepreset?preset=(1-30)
Save current settings to specified preset.<br/>
Example: http://192.168.0.1/savepreset?preset=1
- ### http://192.168.0.1/recallpreset?preset=(1-30)
Recall specified preset.<br/>
Example: http://192.168.0.1/recallpreset?preset=29
- ### http://192.168.0.1/getpresets?addr=(module addr in hex)
Retrieve all 30 presets from the specified module.<br/>
Example (259A): http://192.168.0.1/getpresets?addr=0x28
- ### http://192.168.0.1/setpresets
Write all 30 presets to the specified module.<br/>
Example: http://192.168.0.1/setpresets <br/> </br>
Note this is an HTTP POST with a JSON formatted body. <br/> 
Example HTML here https://github.com/studiohsoftware/2WIRELESS/blob/master/Firmware/SetPresets.html <br/>
Example JSON (for 292e) here https://github.com/studiohsoftware/2WIRELESS/blob/master/Firmware/presetData.json <br/>
- ### http://192.168.0.1/midinoteon?mask=(0x0-0xF)&note=(0-127)&velo=(0-255)
Send MIDI note ON message.<br/> 
Example (bus A, note G1, half velocity): http://192.168.0.1/midinoteon?mask=0x8&note=31&velo=127
- ### http://192.168.0.1/midinoteoff?mask=(0x0-0xF)&note=(0-127)&velo=(0-255)
Send MIDI note OFF message.<br/>
Example (All buses, note F3, max velocity) http://192.168.0.1/midinoteoff?mask=0xF&note=63&velo=255
- ### http://192.168.0.1/midifinetune?mask=(0x0-0xF)&tune=(0x00-0x3F)
Send MIDI fine tune message. <br/>
Example (Bus B, lowest fine tune -49): http://192.168.0.1/midifinetune?mask=0x4&tune=0x00<br/>
Example (Bus C, Ft=An): http://192.168.0.1/midifinetune?mask=0x2&tune=0x32<br/>
Example (Bus D, highest fine tune +49): http://192.168.0.1/midifinetune?mask=0x1&tune=0x63<br/>
- ### http://192.168.0.1/midibend?mask=(0x0-0xF)&bend_lsb=(0x0-0x7F)&bend_msb=(0x0-0x7F)
Send MIDI bend command.<br/>
Example (Bus A, lowest bend): http://192.168.0.1/midibend?mask=0x8&bend_lsb=0x00&bend_msb=0x00<br/>
Example (Bus A, no bend): http://192.168.0.1/midibend?mask=0x8&bend_lsb=0x00&bend_msb=0x40<br/>
Example (Bus A, highest bend): http://192.168.0.1/midibend?mask=0x8&bend_lsb=0x7F&bend_msb=0x7F
- ### http://192.168.0.1/midiclockstart
Midi clock start.
- ### http://192.168.0.1/midiclockstop
Midi clock stop.
- ### http://192.168.0.1/midiclock
Midi clock. 24 per beat required?
- ### http://192.168.0.1/readbyte?addr=(0x0-0x03FFFF)
Read one byte from on-board non-volatile memory at the specified address.<br/>
Example (Address 0x1F. Note 0x001F, or 0x00001F also acceptable): http://192.168.0.1/readbyte?addr=0x1F
- ### http://192.168.0.1/writebyte?addr=(0x0-0x03FFFF)&value=(0x00-0xFF)
Write one byte to on-board non-volatile memory at the specified address.<br/>
Example (Address 0x1ABCD, value 0x0A): http://192.168.0.1/writebyte?addr=0x1ABCD&value=0x0A <br/>
Note all addresses below 0x02C000 are used as scratch space by preset and firmware transfers. 
- ### http://192.168.0.1/writememory
Write bulk data to on-board non-volatile memory.<br/>
Note this is an HTTP POST with a JSON formatted body.<br/> 
Example JSON here https://github.com/studiohsoftware/2WIRELESS/blob/master/Firmware/writeMemoryExample.json<br/>
Notes:
- #### 20k byte JSON file limit.
- #### Adresses are arbitrary locations within 0x0-0x03FFFF.
- #### Data elements can be up to 256 bytes long each (512 characters).
- #### All addresses below 0x02C000 are used as scratch space by preset and firmware transfers. 

