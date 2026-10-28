# Changelog

## b1.7.1 (beta)

- Next Up in landscape: each session box shows the date as well as the day (Sat 31)
- Last Race in portrait: one column with every finishing time; swipe up and down to see the whole field

## b1.7.0 (beta)

- **Beta versions:** firmware is labelled b1.7.0 until the official release; beta boards also take
  beta updates (see [docs/updates.md](docs/updates.md))
- Flasher: clearer **boot mode** help (hold BOOT while you click Connect & install, let go at
  Erasing or Installing), and a wider layout with tips and the device log beside the steps
- The favourite driver list is complete straight after setup, and switches to the current
  standings once LapLogic has loaded them

- **Updates over Wi-Fi** from GitHub releases: LapLogic tells you when there's a new version and
  installs it when you say so (see [Updates over Wi-Fi](docs/updates.md))
- The flasher keeps your Wi-Fi and settings when installing
- Needs one USB install to switch the flash to two firmware slots (Arduino IDE: Partition Scheme
  *Minimal SPIFFS*)

## 1.6.1

- Your team logos now show on the Drivers, Constructors and Last Race pages in every orientation,
  not just in landscape: in narrow rows they replace the thin colour bar

## 1.6.0

- Everything updates as fast as its source allows (see [How fast it updates](docs/using.md#update-speeds)):
  results and standings are checked every minute after a race or sprint until they're out
- 4.3" with an OpenF1 login: race control in the header during live sessions: SAFETY CAR,
  VIRTUAL SC, RED FLAG, yellow flags by sector, then GREEN FLAG and CHEQUERED FLAG for a moment,
  with the header's bottom edge in the flag's colour

## 1.5.0

- CYD: the Track page is gone, which frees memory for everything else
- CYD: rear race light (blue, purple, green, amber, start lights) using the RGB light on the back
- Swiping wraps round from the last page to the first and back, and auto scroll loops the same way

## 1.4.0

- Team badges in team colours on the Drivers page and your driver's card, or your own logos from
  the CYD's MicroSD card
- Light mode and dark mode (a proper light colour scheme, on both boards)
- Brightness slider, on the screen and at laplogic.local
- CYD: auto-dim using the light sensor
- "Auto-rotate" is now called **Auto scroll**, with the time per page on the screen's Settings page
- The CYD's colour-invert fix moved to laplogic.local (*Display colour fix*); the screen's button is
  now Light mode / Dark mode

## 1.3.0

- Fixes for the F1 data not loading on the CYD (`HTTP -1`): the final-lap replay is now kept in
  flash rather than memory, and other memory is freed up, leaving room for secure downloads
- New settings page at `laplogic.local`, and a Settings step in the flasher that works over USB
- Weather at the circuit and chance of rain per session
- Auto-rotate on/off button on the screen's Settings page
- The OpenF1 login only appears for the 4.3" board, the only one that can use it
