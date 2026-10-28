[← LapLogic](../README.md)

# Updates over Wi-Fi

LapLogic checks this project's GitHub releases (`pulsesys-dev/LapLogic`) once a day, and a minute
after it starts. When there's a newer version it asks on screen: **Update available**, with the
release notes and **Install** / **Later**. The laplogic.local page shows the same, with
**Install now** and a **Check for updates** button.

Install downloads the firmware for that board, checks it, and restarts into it, in about a
minute. Keep LapLogic plugged in. If the download fails or is cut short, it keeps running the
version it has. Nothing is ever installed without you choosing to. To stop the daily check,
switch off *Check for updates* at laplogic.local. *Later* asks again the next time LapLogic starts.

**From v1.6.x or earlier, install v1.7.0 over USB once.** Updating over Wi-Fi needs the flash
split into two firmware slots, which only a USB install can set up. After that, updates come over
Wi-Fi. The flasher now keeps your Wi-Fi and settings when it installs, unless you tick *Erase
everything first*. The switch clears the saved track maps and copied logos, which come back by
themselves (logos from the SD card at the next start).

### Publishing an update (for the repository owner)

1. Change `APP_VERSION` in `LapLogic/config.h` and commit. Betas start with **b**
   (`"b1.7.1"`), official releases with **v** (`"v1.8.0"`).
2. Tag the commit with exactly that text and push the tag:
   `git tag b1.7.1 && git push origin b1.7.1`.
3. The **Release** GitHub Action (`.github/workflows/release.yml`) builds both firmwares and
   publishes a release (a **pre-release** for b-tags) with:
   - `LapLogic-*-ota.bin`, which the boards download to update themselves
   - the full `LapLogic-*.bin` images and `LapLogic-Flasher.html`, for USB installs
   - the whole package as a zip
4. Edit the release's description on GitHub if you like. Its first lines show on the screen as
   the release notes.

The repository (or at least its releases) must be public: the boards don't sign in to GitHub. The
Action fails if the tag doesn't match `APP_VERSION`, so a board never sees a version number that
isn't really newer. To build a release on your own computer instead, run `tools/build_release.sh`
(it needs arduino-cli, the ESP32 core 2.0.17, the libraries listed in [Building it yourself](building.md) and esptool). To point the
boards at a different repository, change `UPDATE_REPO` in `config.h`.

### Beta and official versions

While `APP_VERSION` starts with **b**, the firmware is a beta: it shows "(beta)" beside its
version and its update check includes beta releases. When you're ready for an official release,
set `APP_VERSION` to `"v1.8.0"` (or whatever's next) and tag `v1.8.0`. Beta boards then update to
it, since an official version counts as newer than the beta with the same number, and from then on
they only take official releases.
