# Bluetooth speakers and headphones (BlueV)

onionHEN supports [BlueV](https://github.com/0xZ3n0x/BlueV) as an **optional external
plugin**. BlueV captures PS5 game audio and streams it to A2DP speakers and
headphones using the SBC codec. It provides its own pairing/adapter controls,
so onionHEN does **not** reimplement or embed BlueV's Bluetooth stack.

This is an integration of an independently maintained project, not a built-in
driver. The ELF packaging step below adds onionHEN's plugin descriptor without
changing BlueV's executable segments.

## Prepare the plugin on your computer

1. Download the **`bluev.elf`** asset from [BlueV releases](https://github.com/0xZ3n0x/BlueV/releases)
   and verify the accompanying `bluev.elf.sha256`, **or** build it from
   [BlueV's source](https://github.com/0xZ3n0x/BlueV#build) using
   `make release`.
2. Have `llvm-objcopy` available (from the PS5 Payload SDK, or on your PATH).
3. From your onionHEN checkout run:

   ```sh
   python3 scripts/package_bluev_plugin.py /path/to/bluev.elf BLUE00001.elf
   ```

   You can set `PS5_PAYLOAD_SDK` or pass `--objcopy /path/to/llvm-objcopy`
   if the tool isn't on your PATH.

4. Transfer the resulting ELF to:

   ```text
   /data/OnionHEN/plugins/BLUE00001.elf
   ```

Do **not** copy the original `bluev.elf` directly into `plugins/`: onionHEN
requires a valid `.onion_plugin` section. The script checks its input and output,
uses a stable plugin ID (`BLUE00001`), and sets the **stop-supported** flag so
onionHEN sends SIGTERM when stopping BlueV. It deliberately does *not* set
the always-running/restart flag, so BlueV's own **Stop BlueV** action works.

The displayed plugin descriptor version is `1.00` (wrapper ABI version), **not**
the version of BlueV you packaged. BlueV reports its real version in its own UI.

## Use it on the PS5

1. Open **OnionHEN Toolbox → Plugins → BlueV Bluetooth Audio**.
2. Turn on **Run**, then open **Bluetooth audio settings** on that same plugin page.
3. Choose **Start USB** (recommended) or **Start internal PS5 Bluetooth**.
4. Press **Scan for Bluetooth speakers/headphones**. BlueV scans asynchronously
   (up to ~30 seconds), so select **Refresh status and devices** after the scan.
5. Select **Connect: <speaker name>** to pair and start streaming. Use **Disconnect**
   to end the stream. Previously paired devices can reconnect automatically.

The native PS5 page also provides **Forget**, high-quality audio, Bluetooth
adapter auto-start (USB only), AVRCP headphone volume (+/- 10%), and the
**Limit BlueV access to this PS5** toggle. The latter is recommended since
BlueV's HTTP API otherwise has no authentication.

**OnionHEN plugin Auto-start** starts the BlueV process. **Auto-start USB
adapter** in the BlueV page starts your selected USB dongle inside BlueV
when that process boots. The PS5's internal radio can only start manually.

The native settings page uses BlueV's existing localhost HTTP API: it does
**not** require opening BlueV's web page. The upstream `BlueV` home-screen
shortcut and `http://<ps5-ip>:7733/` web interface still exist as an optional
fallback (unless you restrict access to the console). There is no automatic
live refresh while you remain on the page; use **Refresh** to update device
and connection state.

Pairings, settings and logs remain in `/data/bluev/`, managed by BlueV.
For USB adapters that require firmware (notably Realtek), follow
[BlueV's adapter firmware instructions](https://github.com/0xZ3n0x/BlueV#realtek-adapters).

To stop BlueV, toggle **Run** off in onionHEN's plugin settings, or use
BlueV's optional web UI **Stop BlueV** command. If you don't want the
upstream BlueV home-screen shortcut, you can remove it using the upstream
web interface before uninstalling; the native UI currently does not manage
shortcuts. Stop and delete the plugin in onionHEN. Removing the plugin does not erase
`/data/bluev/` pairings and settings.

## Important limitations

- **USB Bluetooth adapters are recommended.** Using the PS5's internal radio
  shares hardware with the DualSense controller and can interrupt controller
  input, potentially until a console restart.
- BlueV supports A2DP output (SBC), **not microphone audio**.
- The BlueV web page has **no authentication** and is network-accessible by
  default. Use a trusted LAN or select its **Only this console** access mode.
- The native Toolbox integration has host-side UI/route tests; the actual
  ShellUI-to-BlueV HTTP calls still need PS5 hardware validation, including
  USB pairing, connection state, restarts, and quality/volume controls.
- BlueV is GPL-3.0-or-later and is independently maintained by 0xZ3n0x.
  Source, releases, troubleshooting and supported hardware are documented in
  the [upstream BlueV repository](https://github.com/0xZ3n0x/BlueV).

Run the packaging tests with Python 3.10+. The ELF execution and real
plugin-inspector integration test requires Linux x86-64, a C/C++ compiler
and `llvm-objcopy` (the descriptor unit tests run on other hosts):

```sh
python3 -m unittest discover -s scripts/tests -p 'test_package_bluev_plugin.py'
```
