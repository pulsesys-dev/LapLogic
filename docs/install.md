[← LapLogic](../README.md)

# Installing LapLogic

## From your browser (easiest)

You need **Chrome or Edge** on a Mac, Windows or Linux computer, and a USB **data** cable.

1. Download **`LapLogic-Flasher.html`** from the [latest release](https://github.com/pulsesys-dev/LapLogic/releases/latest)
   and open it in Chrome or Edge (double-click it, or drag it into a browser window). It works offline; everything is inside that one file.
2. Choose your board.
3. Plug the board in, click **Connect & install** and pick its port from the list
   (often "USB Serial", "CH340", "CP210x" or "USB JTAG/serial debug unit").
4. When it's installed, the page asks for your Wi-Fi network and password and sends them to
   LapLogic over the same cable. (Or skip that and use the QR code on the screen.)
5. **Settings** (optional): the page then reads LapLogic's settings over the cable. Pick your
   favourite driver, time zone, orientation, dark or light mode, brightness, auto scroll and so on, and click **Save to LapLogic**.
   On the 4.3" board this is also where the OpenF1 login for live positions goes.

Already installed? Use **"Just set up Wi-Fi"** on the page to change networks, or
**Load settings from LapLogic** in step 4 to change settings.

If something goes wrong, open **Device log** on the page. It shows everything the
board prints; use *Copy* or *Save as file* to share it.

**If Connect fails**, use **boot mode**. The page shows these steps under *"Board won't connect?"*:

1. Press and **hold** the **BOOT** button.
2. While holding it, click **Connect & install** and pick the port.
3. Keep holding until the progress bar says **Erasing** or **Installing**, then let go.

Still nothing? Unplug the board, hold BOOT while you plug it back in, then click **Connect &
install**. Ticking **Safe speed** helps with long or cheap cables.

**If the cable comes out** part-way through, plug it back in. During install, click
**Retry install** (a half-finished install is simply written again). During Wi-Fi setup the page
reconnects by itself, or click **Reconnect**.

<details>
<summary>Alternative: Espressif's online flasher</summary>

Open https://espressif.github.io/esptool-js/, click **Connect**, set *Flash Address* to `0x0`,
choose the `.bin` for your board from the [latest release](https://github.com/pulsesys-dev/LapLogic/releases/latest), then click **Program**.
</details>


## First-time setup

**CYD only - touch calibration.** The first time it starts, the CYD shows three red crosses one
after another. Tap the centre of each with a fingernail or stylus. This takes ten seconds and is
remembered. (The Guition's touch needs no calibration.)

**Wi-Fi:**

1. The screen shows a QR code. Scan it with your phone's camera to join the Wi-Fi hotspot
   **LapLogic-Setup** (or join it manually from your Wi-Fi settings).
2. A setup page opens. If it doesn't, open a browser and go to **192.168.4.1**.
3. Tap **Configure WiFi**, choose your home network, type the password and save.
4. LapLogic connects, works out your time zone and loads the F1 data within a few seconds.

> Both boards only support **2.4 GHz** Wi-Fi. Most home routers broadcast both; if yours has
> separate names, pick the 2.4 GHz one.


## Upgrading from PitWall

LapLogic was called PitWall before v1.2.0. Install it over the top with the flasher. Your Wi-Fi,
settings and CYD touch calibration are kept. The settings address is now
`http://laplogic.local`, and the setup hotspot is called `LapLogic-Setup`.
