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
- ### http://192.168.0.1/setpresets?addr=(module addr in hex)
Command specified module to read all 30 presets from on-board non-volatile memory.<br/>
Example (259A): http://192.168.0.1/setpresets?addr=(0x28) <br/> </br>
Use writememory to send presets to on-board non-volatile memory first.
Example HTML here https://github.com/studiohsoftware/2WIRELESS/blob/master/Firmware/SetPresets.html <br/>
Example JSON (for 292e) here https://github.com/studiohsoftware/2WIRELESS/blob/master/Firmware/presetData.json <br/>
- ### http://192.168.0.1/midinoteon?chan=(1-16)&note=(0x0-0x7F)&velo=(0x0-0x7F)
Send MIDI note ON message.<br/> 
Example (Channel 9, note G1, half velocity): http://192.168.0.1/midinoteon?chan=9&note=0x1F&velo=0x40
- ### http://192.168.0.1/midinoteoff?chan=(1-16)&note=(0x0-0x7F)&velo=(0x0-0x7F)
Send MIDI note OFF message.<br/>
Example (Channel 16, note F3, max velocity) http://192.168.0.1/midinoteoff?chan=16&note=0x35&velo=0x7F
- ### http://192.168.0.1/midifinetune?chan=(1-16)&tune=(0x00-0x3F)
Send MIDI fine tune message. <br/>
Example (Channel 1, lowest fine tune -49): http://192.168.0.1/midifinetune?chan=1&tune=0x00<br/>
Example (Channel 2, Ft=An): http://192.168.0.1/midifinetune?chan=2&tune=0x32<br/>
Example (Channel 3, highest fine tune +49): http://192.168.0.1/midifinetune?chan=3&tune=0x63<br/>
- ### http://192.168.0.1/midibend?chan=(1-16)&bend_lsb=(0x0-0x7F)&bend_msb=(0x0-0x7F)
Send MIDI bend command.<br/>
Example (Channel 4, lowest bend): http://192.168.0.1/midibend?chan=4&bend_lsb=0x00&bend_msb=0x00<br/>
Example (Channel 5, no bend): http://192.168.0.1/midibend?chan=5&bend_lsb=0x00&bend_msb=0x40<br/>
Example (Channel 6, highest bend): http://192.168.0.1/midibend?chan=6&bend_lsb=0x7F&bend_msb=0x7F
- ### http://192.168.0.1/midiclockstart
Midi clock start.
- ### http://192.168.0.1/midiclockstop
Midi clock stop.
- ### http://192.168.0.1/midiclock
Midi clock. 24 per beat required.
- ### http://192.168.0.1/sendmidibytes?byte1=(0-0xFF)&byte2=(0x0-0x7F)&byte3=(0x0-0x7F)
Send MIDI command as bytes<br/>
Example (Program Change to change preset to 7):</br>
 http://192.168.0.1/sendmidibytes?byte1=C0&byte2=0x07&byte3=0x00<br/>
Example (Control Change to set fine tune to An on channel 5): </br>
http://192.168.0.1/sendmidibytes?byte1=B4&byte2=0x1F&byte3=0x32<br/>
- ### http://192.168.0.1/ssid
Get wireless SSID<br/>
Returns BUCHLA200E by default.
- ### http://192.168.0.1/ssid=(SSID string)
Set wireless SSID<br/>
Example: http://192.168.0.1/ssid=TEST<br/>
Note ssid limited to 32 characters. 
- ### http://192.168.0.1/password
Get wireless WPA password<br/>
Returns BUCHLA200E by default.
- ### http://192.168.0.1/password=(password string)
Set wireless WPA password<br/>
Example: http://192.168.0.1/password=TEST<br/>
Note password must be at least 8 characters and no more than 63 characters.
- ### http://192.168.0.1/presetname?preset=(1-30)
Get preset name<br/>
Example: get name for preset 12 http://192.168.0.1/presetname?preset=12<br/>
- ### http://192.168.0.1/presetname=(preset name string)&preset=(1-30)
Set preset name<br/>
Example: Set name for preset 5 to "Test" http://192.168.0.1/presetname=Test&preset=5<br/>
- ### http://192.168.0.1/velo?chan=(1-16)
Get velocity enable/disable for MIDI channel.<br/>
Example: Get velocity enable value for channel 2 http://192.168.0.1/velo?chan=2<br/>
- ### http://192.168.0.1/velo=(0-1)&chan=(1-16)
Set velocity enable/disable value for MIDI channel.<br/>
Example: Enable velocity on channel 1 http://192.168.0.1/velo=1&chan=1<br/>
- ### http://192.168.0.1/poly?chan=(1-16)
Get poly enable/disable for MIDI channel.<br/>
Example: Get poly enable value for channel 2 http://192.168.0.1/poly?chan=2<br/>
- ### http://192.168.0.1/poly=(0-1)&chan=(1-16)
Set poly enable/disable value for MIDI channel.<br/>
Example: Enable poly on channel 1 http://192.168.0.1/poly=1&chan=1<br/>
- ### http://192.168.0.1/tran?chan=(1-16)
Get transpose value for MIDI channel.<br/>
Example: Get transpose value for channel 3 http://192.168.0.1/tran?chan=3<br/>
- ### http://192.168.0.1/tran=(-49 to 49)&chan=(1-16)
Set transpose value for MIDI channel.<br/>
Example: Set transpose to -30 on channel 1 http://192.168.0.1/tran=-30&chan=1<br/>
- ### http://192.168.0.1/fine?chan=(1-16)
Get fine tune value for MIDI channel.<br/>
Example: Get fine tune value for channel 3 http://192.168.0.1/fine?chan=3<br/>
- ### http://192.168.0.1/fine=(-49 to 49)&chan=(1-16)
Set fine tune value for MIDI channel.<br/>
Example: Set fine tune to An on channel 1 http://192.168.0.1/fine=0&chan=1<br/>
Example: Set fine tune to 30 on channel 5 http://192.168.0.1/fine=30&chan=5<br/>
- ### http://192.168.0.1/mask?chan=(1-16)
Get bus mask for MIDI channel.<br/>
Example: Get mask value for channel 3 http://192.168.0.1/mask?chan=3<br/>
- ### http://192.168.0.1/mask=(0x0-0xF)&chan=(1-16)
Set mask value for MIDI channel.<br/>
Example: Route channel 1 to all four 200e buses http://192.168.0.1/mask=0xF&chan=1<br/>
Example: Route channel 1 to bus A http://192.168.0.1/mask=0x8&chan=1<br/>
Example: Route channel 1 to bus B http://192.168.0.1/mask=0x4&chan=1<br/>
Example: Route channel 1 to bus C http://192.168.0.1/mask=0x2&chan=1<br/>
Example: Route channel 6 to bus D http://192.168.0.1/mask=0x1&chan=6<br/>
- ### http://192.168.0.1/readmemory?addr=(0x0-0x03FFFF)&length=0-262143
Read bulk data from on-board non-volatile memory.<br/>
Example (Address 0x1C, return 32 bytes. Note 0x001C, or 0x00001C also acceptable): http://192.168.0.1/readmemory?addr=0x1C&length=32
- ### http://192.168.0.1/writememory?addr=(0x0-0x03FFFF)&data=(hex string)
Write data to on-board non-volatile memory at the specified address.<br/>
Example: (Address 0x1ABCD, five data bytes 0AFF1B0432) http://192.168.0.1/writememory?addr=0x1ABCD&data=0AFF1B0432<br/>
Note all addresses below 0x02C000 are used as scratch space by preset and firmware transfers. 
- ### http://192.168.0.1/writememory
Write bulk data to on-board non-volatile memory.<br/>
Note this is an HTTP POST with a JSON formatted body.<br/> 
Example JSON here https://github.com/studiohsoftware/2WIRELESS/blob/master/Firmware/writeMemoryExample.json<br/>
Notes:
- #### 20k byte JSON file limit.
- #### Addresses are arbitrary locations within 0x0-0x03FFFF.
- #### Data elements can be up to 256 bytes long each (512 characters).
- #### All addresses below 0x02C100 are used as scratch space by preset and firmware transfers. 
## V2 Firmware Support
Old firmware (pre Primo version) also supported. Simply add a "v2" to the URL path. <br/>
Example: http://192.168.0.1/v2/remotedisable<br/>
