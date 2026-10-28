[← LapLogic](../README.md)

# Troubleshooting

| Problem | Fix |
|---|---|
| Screen upside down | Settings page → *Flip screen* |
| Want it standing tall | Settings page → *Portrait* (or *Landscape* to go back) |
| CYD: asks to calibrate after switching to portrait | Expected: touch is calibrated per orientation. Tap the three crosses once |
| Guition: taps land diagonally opposite in portrait | In `config.h`, set `TOUCH_PORTRAIT_FIX` to `1` and rebuild |
| Track page says the map appears once OpenF1 has data | OpenF1 hasn't published anything for that circuit yet; it fills in after the first session |
| Live map: "login refused" | Check the OpenF1 email and password at `http://laplogic.local` |
| CYD: taps land in the wrong place | Settings page → *Calibrate touch*, or hold the screen for 4 s at start-up |
| CYD: colours look wrong (black shows white, red shows light blue) | At `http://laplogic.local`, set *Display colour fix* to *Inverted* (or *Normal*). This is a fix for the screen itself, separate from light mode. LapLogic detects most panels that need this, but new batches turn up |
| CYD: red and blue swapped | At `http://laplogic.local`, set *Display type* to the other option. Two-USB-port CYDs use an ST7789; LapLogic normally detects this itself |
| Guition: taps land in the wrong place | In `config.h`, set `TOUCH_MIRROR_X` and/or `TOUCH_MIRROR_Y` to `1` and rebuild |
| Screen stays black or garbled | In `config.h`, lower `LCD_SPEED_HZ` for your board and rebuild |
| Times are an hour out | Daylight saving: choose your city instead of *Automatic* in settings |
| Pages keep changing by themselves | Settings page → *Scroll: On* to turn it off |
| CYD: the screen gets brighter as the room gets darker | In `config.h`, set `LDR_DARK_IS_HIGH` to `0` and rebuild. The `[ldr]` log lines show the sensor's readings |
| CYD: auto-dim hardly changes anything | Lower `LDR_MIN_SPAN` in `config.h`. It learns the brightest and darkest the room has been since start-up, so it settles in after a day/night cycle |
| A team logo doesn't show | See the `[logo]` lines in the log: each file is listed with its size or the reason it was skipped. Check the file name and format (see [Team logos](using.md#team-logos)) and restart |
| No F1 data; the log shows `[f1] ... -> HTTP -1` | The board couldn't open a secure connection. The log line ends with the reason and the free memory. Usually memory (CYD: check `largest block` in the `[mem]` lines) or the network blocking `api.jolpi.ca`. Restart the board; if it keeps happening, send the log |
| No weather shown | It appears once the calendar has loaded. The log shows `[wx]` lines; the board needs to reach `api.open-meteo.com` |
| `laplogic.local` doesn't open | Some networks and older Android phones don't support `.local` names. Use the IP address shown on the Settings page instead |
| "Couldn't reach your saved Wi-Fi" | Check the router is on and 2.4 GHz is enabled, or re-run setup |
| No port shows up on a Mac | Try another cable (many are charge-only). Boards with a CH340 USB chip may need the CH34x driver from WCH's website |
| Upload fails | Hold **BOOT**, tap **RST**, release BOOT, upload again (the flasher page shows these steps) |

To see what LapLogic is doing, open the Serial Monitor at **115200** baud (or the Device log on
the flasher page after "Just set up Wi-Fi"). It logs a `[mem]` line every minute; include those
if you report a problem. On the CYD, `largest block` should stay above about 20000. That's the
room it needs for each secure download of F1 data. Every data refresh is logged too (`[f1] ...`),
so the log alone shows whether the data is staying current. Weather refreshes are logged as
`[wx] ...` and the track map as `[map] ...`.

