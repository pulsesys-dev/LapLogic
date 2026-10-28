// Improv Wi-Fi over serial (https://www.improv-wifi.com/serial/)
// Lets a web page (e.g. ESP Web Tools) send Wi-Fi credentials over USB.
#pragma once
#include <Arduino.h>
#include "settings.h"

class ImprovSerial {
public:
  explicit ImprovSerial(Stream &s) : _s(s) {}
  void loop();

  // called by the network code when a provisioning attempt finishes
  void provisioned(const String &url);
  void failed();

private:
  enum : uint8_t { T_STATE = 1, T_ERROR = 2, T_RPC = 3, T_RESULT = 4 };
  enum : uint8_t { ST_READY = 2, ST_PROVISIONING = 3, ST_PROVISIONED = 4 };
  enum : uint8_t { E_NONE = 0, E_INVALID = 1, E_UNKNOWN_CMD = 2, E_CONNECT = 3 };

public:
  void sendResult(uint8_t cmd, const String *strs, uint8_t n);
private:
  void handle(uint8_t type, const uint8_t *d, uint8_t len);
  void sendPacket(uint8_t type, const uint8_t *d, uint8_t len);
  void sendState(uint8_t st);
  void sendError(uint8_t e);
  bool     _pending = false;   // settings changed over USB but not applied yet
  Settings _before;

  Stream &_s;
  uint8_t _buf[300];
  size_t  _len = 0;
};
