# 2WIRELESS
# Wireless Preset Manager
### Supported REST Calls (so far)
- http://192.168.0.1/remoteenable
- http://192.168.0.1/remotedisable
- http://192.168.0.1/savepreset?preset=(0-29)
- http://192.168.0.1/recallpreset?preset=(0-29)
- http://192.168.0.1/getpresets?addr=(module addr in hex)
- http://192.168.0.1/midinoteon?mask=(0x0-0xF)&note=(0-127)&velo=(0-255)
- http://192.168.0.1/midinoteoff?mask=(0x0-0xF)&note=(0-127)&velo=(0-255)
- http://192.168.0.1/midibend?mask=(0x0-0xF)&bend_lsb=(0x0-0x7F)&bend_msb(0x0-0x7F)
