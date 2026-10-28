// Wi-Fi: setup hotspot + settings web page (WiFiManager), Improv over USB,
// network time, mDNS and automatic time zone detection.
#pragma once
#include <Arduino.h>

class ImprovSerial;

enum NetState : uint8_t { NET_CONNECTING, NET_SETUP, NET_ONLINE, NET_OFFLINE };

void     netBegin(bool forceSetup);
void     netLoop();
NetState netState();
String   netUrl();          // "http://192.168.1.50"
String   netSsid();
int      netRssi();
String   netTzLabel();      // e.g. "Australia/Brisbane (auto)"
void     netDetectTimezone();    // look the time zone up again (after choosing "Automatic")
void     netResetWifi();         // forget Wi-Fi and reboot into setup
void     netImprovConnect(const String &ssid, const String &pass, ImprovSerial *who);
bool     netTimeValid();
