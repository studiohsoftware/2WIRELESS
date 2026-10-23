// Shim: USB_Host_Library_SAMD (usbhub.cpp) includes "delay.h", which arduino:samd >= 1.8.10 no longer ships.
// delay()/delayMicroseconds() are declared by Arduino.h.
#pragma once
#include <Arduino.h>
