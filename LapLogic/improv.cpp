#include "improv.h"
#include "config.h"
#include "net.h"
#include "settings.h"
#include "ui.h"
#include <WiFi.h>

static const char HDR[6] = {'I', 'M', 'P', 'R', 'O', 'V'};

void ImprovSerial::loop() {
  while (_s.available()) {
    uint8_t b = _s.read();

    if (_len < 6) {                         // header
      if (b == (uint8_t)HDR[_len]) _buf[_len++] = b;
      else _len = (b == 'I') ? (_buf[0] = b, 1) : 0;
      continue;
    }
    if (_len == 6 && b != 1) { _len = 0; continue; }   // protocol version 1 only

    _buf[_len++] = b;
    if (_len >= 9) {
      const size_t total = 9 + _buf[8] + 1;           // header+ver+type+len, data, checksum
      if (total > sizeof(_buf)) { _len = 0; continue; }
      if (_len == total) {
        uint8_t sum = 0;
        for (size_t i = 0; i < total - 1; i++) sum += _buf[i];
        if (sum == _buf[total - 1]) handle(_buf[7], _buf + 9, _buf[8]);
        else sendError(E_INVALID);
        _len = 0;
      }
    }
  }
}

void ImprovSerial::sendPacket(uint8_t type, const uint8_t *d, uint8_t len) {
  uint8_t out[9 + 256 + 2];
  memcpy(out, HDR, 6);
  out[6] = 1;
  out[7] = type;
  out[8] = len;
  memcpy(out + 9, d, len);
  uint8_t sum = 0;
  for (size_t i = 0; i < 9u + len; i++) sum += out[i];
  out[9 + len] = sum;
  out[10 + len] = '\n';
  _s.write(out, 11 + len);
  _s.flush();
}

void ImprovSerial::sendState(uint8_t st) { sendPacket(T_STATE, &st, 1); }
void ImprovSerial::sendError(uint8_t e)  { sendPacket(T_ERROR, &e, 1); }

void ImprovSerial::sendResult(uint8_t cmd, const String *strs, uint8_t n) {
  uint8_t d[256];
  size_t p = 2;
  for (uint8_t i = 0; i < n; i++) {
    size_t l = min((size_t)strs[i].length(), (size_t)200);
    if (p + 1 + l > sizeof(d)) break;
    d[p++] = (uint8_t)l;
    memcpy(d + p, strs[i].c_str(), l);
    p += l;
  }
  d[0] = cmd;
  d[1] = (uint8_t)(p - 2);
  sendPacket(T_RESULT, d, (uint8_t)p);
}

void ImprovSerial::provisioned(const String &url) {
  sendState(ST_PROVISIONED);
  sendResult(0x01, &url, 1);
}

void ImprovSerial::failed() {
  sendError(E_CONNECT);
  sendState(ST_READY);
}

void ImprovSerial::handle(uint8_t type, const uint8_t *d, uint8_t len) {
  if (type != T_RPC || len < 2) { sendError(E_INVALID); return; }
  const uint8_t cmd = d[0];
  const uint8_t plen = d[1];
  const uint8_t *p = d + 2;
  if (plen + 2 > len) { sendError(E_INVALID); return; }

  switch (cmd) {
    case 0x01: {  // Wi-Fi settings: ssid_len, ssid, pass_len, pass
      if (plen < 1) { sendError(E_INVALID); return; }
      uint8_t sl = p[0];
      if (1 + sl + 1 > plen) { sendError(E_INVALID); return; }
      String ssid, pass;
      for (uint8_t i = 0; i < sl; i++) ssid += (char)p[1 + i];
      uint8_t pl = p[1 + sl];
      if (2 + sl + pl > plen) { sendError(E_INVALID); return; }
      for (uint8_t i = 0; i < pl; i++) pass += (char)p[2 + sl + i];
      sendError(E_NONE);
      sendState(ST_PROVISIONING);
      netImprovConnect(ssid, pass, this);
      break;
    }
    case 0x02: {  // current state
      if (netState() == NET_ONLINE) {
        String url = netUrl();
        sendState(ST_PROVISIONED);
        sendResult(0x02, &url, 1);
      } else {
        sendState(ST_READY);
      }
      break;
    }
    case 0x03: {  // device info
#if BOARD_CYD
      String info[4] = {APP_NAME, APP_VERSION, "ESP32", APP_NAME};
#else
      String info[4] = {APP_NAME, APP_VERSION, "ESP32-S3", APP_NAME};
#endif
      sendResult(0x03, info, 4);
      break;
    }
    case 0x04: {  // scan networks
      int n = WiFi.scanNetworks();
      for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;
        bool dup = false;
        for (int j = 0; j < i; j++) if (WiFi.SSID(j) == ssid) { dup = true; break; }
        if (dup) continue;
        String row[3] = {ssid, String(WiFi.RSSI(i)),
                         WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"};
        sendResult(0x04, row, 3);
      }
      WiFi.scanDelete();
      sendResult(0x04, nullptr, 0);   // end of list
      break;
    }
    // ---- LapLogic extensions: settings over USB (used by LapLogic-Flasher.html) ----
    case 0x81: {  // read every setting: one result per [key, value], then an empty result
      settingsForEach([](void *ctx, const char *key, const String &value) {
        String kv[2] = {key, value};
        ((ImprovSerial *)ctx)->sendResult(0x81, kv, 2);
      }, this);
      if (const F1Data *d = uiData())
        for (int i = 0; i < d->nDrivers; i++) {
          String kv[2] = {"driver", String(d->drivers[i].code) + " " + d->drivers[i].last};
          sendResult(0x81, kv, 2);
        }
      sendResult(0x81, nullptr, 0);
      break;
    }
    case 0x82: {  // set one setting: strings [key, value]; reply [key, "ok" or the reason]
      if (plen < 2 || p[0] + 2 > plen || p[0] + 2 + p[1 + p[0]] > plen) { sendError(E_INVALID); return; }
      const uint8_t kl = p[0], vl = p[1 + kl];
      String key, value;
      for (uint8_t i = 0; i < kl; i++) key += (char)p[1 + i];
      for (uint8_t i = 0; i < vl; i++) value += (char)p[2 + kl + i];
      if (!_pending) { _before = settings; _pending = true; }
      const String why = settingsSet(settings, key.c_str(), value);
      String reply[2] = {key, why.length() ? why : String("ok")};
      sendResult(0x82, reply, 2);
      break;
    }
    case 0x83: {  // apply what was set: reply "ok", or "restart" if LapLogic is about to restart
      const bool restart = _pending && settingsApply(_before);
      _pending = false;
      String reply = restart ? "restart" : "ok";
      sendResult(0x83, &reply, 1);
      break;
    }
    default:
      sendError(E_UNKNOWN_CMD);
  }
}
