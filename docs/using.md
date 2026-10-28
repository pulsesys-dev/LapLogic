[← LapLogic](../README.md)

# Using LapLogic

**Pages** (swipe left/right): Next Up · Track · Drivers · Constructors · Last Race · Calendar · Settings.
The CYD has no Track page: its screen and memory can't do the live map justice, so it's left out.
Swiping wraps round: past Settings takes you back to Next Up, and back from Next Up goes to
Settings. Auto scroll goes through every page except Calendar and Settings, then starts again
(the Track page stays up long enough for the replay to play through); touching the screen pauses
it for a minute. To stop it, tap **Scroll: On** on the
Settings page (it changes to *Off*); the button beside it (**20 s / page**) steps through the time
on each page. Both are also at laplogic.local. On the CYD, auto scroll switches pages
instantly rather than sliding, to save memory.

**Settings page on the screen:** flip the display upside down, switch between **Portrait** and
**Landscape** (LapLogic restarts to re-lay out the screens), turn auto scroll on or off and set its
time, switch between **Light mode** and **Dark mode**, drag the **Brightness** slider, force a data
refresh, reset Wi-Fi, and on the CYD, switch **Auto** dimming on or off and recalibrate touch.

**Settings from your phone or PC:** while LapLogic is online, open **http://laplogic.local**
(or the IP address shown on the Settings page). The same settings are in step 4 of the flasher
page, over USB. You can set:
- Favourite driver (picked from the current grid)
- Time zone (Automatic, or choose one), 12 or 24-hour clock
- Weather units: °C and km/h, or °F and mph
- Orientation (landscape or portrait; on the CYD, switching asks you to recalibrate touch once),
  upside down, dim at night
- Dark mode or light mode, brightness, and (CYD) auto-dim
- Auto scroll on or off, and the auto scroll time (seconds per page)
- 4.3" only: OpenF1 login for live track positions and track temperature
- CYD only: display type and colours (see [Troubleshooting](troubleshooting.md))

The page also shows the board's status (Wi-Fi, when the F1 data last updated, the weather,
free memory) and has buttons to refresh the data, restart, recalibrate touch (CYD) or change
Wi-Fi network.

**Weather:** the Next Up page shows the temperature and conditions at the next circuit beside its
name, and each session's chance of rain under its time (once the weekend is within the 16-day
forecast). During a race weekend the Track page (4.3" board) adds air temperature, wind and rain chance; on
the 4.3" board with an OpenF1 login, it shows the live track temperature during sessions too.
The weather comes from Open-Meteo and refreshes every 15 minutes.

**At start-up**, while the logo is showing:
- **Tap** the screen to go back to Wi-Fi setup
- **Hold** for 4 seconds (CYD only) to recalibrate touch, useful if taps are so far off you
  can't reach the Settings button

<a id="track-map"></a>**Track map (4.3" Guition only):**
- **Free, no account:** during a race weekend (from two days before practice) it shows that
  weekend's circuit. After a race or sprint, it adds a looping replay of every car's position
  over the leader's final lap, with the result beside or under the map on the 4.3" board.
  Between weekends it shows the last race's replay. Circuit outlines are drawn from real car
  positions and saved on the board, so each one downloads only once.
- **Live, 4.3" board only:** during sessions, cars move on the map in real time with the running
  order beside them, and race control flags (safety car, red flag, yellow sectors) appear in the
  header on every page. This needs an [OpenF1 sponsor login](https://openf1.org/auth.html), because
  OpenF1 only offers real-time data to sponsors. Enter it at `http://laplogic.local` or in step 4 of the flasher.
- OpenF1 makes each session's data free shortly after it ends, so the replay appears a little
  while after the chequered flag.

<a id="race-light"></a>**Rear race light (CYD):** the RGB light on the back of the board shows what's
happening, so a clear 3D-printed back can glow behind the display:

| When | Light |
|---|---|
| Practice is on | blue |
| Qualifying or sprint qualifying is on | purple |
| 10 minutes before a race or sprint | slow amber pulse |
| The last 10 seconds | red, getting brighter (the start lights) |
| Lights out, then the race or sprint is on | off for a moment, then green |
| Nothing on | off |

It works from the published session times. A race is shown as on for two hours from the start,
so the light can stay green a little after the flag. It follows the screen's brightness,
including night dimming and auto-dim. Turn it off with *Rear race light* at laplogic.local;
*Test rear light* there cycles through every colour so you can check your housing. If it's too
bright behind the plastic, lower `LED_MAX` in `config.h`. It can't show yellow flags, safety cars
or red flags: those need OpenF1's live race control feed, which needs a sponsor login.

<a id="team-logos"></a>**Team logos (CYD, your own images):** LapLogic shows team colour badges by
default. To show logos instead, add your own image files to the CYD's MicroSD card:

1. On a FAT32-formatted card, make a folder called `logos`.
2. Add one PNG per team, named after the team: `mclaren.png`, `ferrari.png`, `red_bull.png`,
   `mercedes.png`, `aston_martin.png`, `alpine.png`, `williams.png`, `racing_bulls.png`,
   `haas.png`, `audi.png`, `cadillac.png`. Spaces, capitals and underscores don't matter
   (`Red Bull.png` works too).
3. Use a transparent background and up to 128 × 128 pixels (64 × 64 is plenty). Save as an
   ordinary 8-bit, non-interlaced PNG, which is what most editors do by default.
4. Put the card in and restart LapLogic.

At start-up LapLogic copies the logos into its own flash, shrunk to badge size, so you can take the
card out afterwards and they stay. To change them, edit the card and restart; to remove them all,
restart with an empty `logos` folder. Logos appear on Next Up (your driver's card), Drivers,
Constructors and Last Race; teams without a logo keep their coloured badge or bar. The Settings
page at laplogic.local shows how many logos were found. The 4.3" Guition doesn't read the SD card,
so it always uses colour badges. Logos are for your own use; don't share a package with them in it.

**Brightness:** the slider sets the brightness you want. On the CYD, **Auto** scales that down as
the room gets darker (down to 15% of your setting), using the light sensor beside the screen.
*Dim at night* still applies on top. Changes fade in over about half a second.

**If your Wi-Fi goes down:** LapLogic keeps showing the last data and reconnects by itself.
If it can't reach your saved network at start-up, it opens the setup hotspot for 5 minutes
in case you've changed routers, then goes back to retrying.

---

<a id="update-speeds"></a>
## How fast it updates

Everything is set to the fastest each service usefully changes:

| Data | Source | How often |
|---|---|---|
| Live car positions, running order, track temp, race control flags (4.3", OpenF1 login) | OpenF1 live stream | instantly, as OpenF1 sends them (positions about 4 times a second) |
| Results and standings after a race or sprint | Jolpica | every minute from about the flag until the results are published (up to 8 hours) |
| Standings and last result, otherwise | Jolpica | every 10 minutes |
| Calendar | Jolpica | hourly |
| Weather | Open-Meteo | every 15 minutes (it recalculates every 15 minutes) |
| Track map and final-lap replay (4.3") | OpenF1, free | every minute during a session, 2 minutes on a race weekend, otherwise 15 |

The intervals are in `config.h` (`STANDINGS_REFRESH_MS` and the rest). All of this stays well inside
each service's limits. The live feed runs a few seconds behind live timing. When a season ends, LapLogic shows the champions and switches to the
next season's calendar as soon as it's published.
