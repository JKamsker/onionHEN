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
2. Turn on **Run**. Optionally enable **Auto-start** for future onionHEN boots.
3. BlueV installs a **BlueV** home-screen entry that opens its control page.
   Alternatively, visit `http://<ps5-ip>:7733/` on your local network.
4. On that page, select a Bluetooth adapter, press **Start**, then **Scan**
   and **Connect** your speaker/headphones.

Pairings, settings and logs remain in `/data/bluev/`, managed by BlueV.
For USB adapters that require firmware (notably Realtek), follow
[BlueV's adapter firmware instructions](https://github.com/0xZ3n0x/BlueV#realtek-adapters).

To stop BlueV cleanly, use its own **Stop BlueV** button; you can also toggle
**Run** off in the onionHEN Toolbox. Before uninstalling, use BlueV's web
control to remove its home-screen shortcut if you don't want to keep it, then
stop and delete the plugin in onionHEN. Removing the plugin does not erase
`/data/bluev/` pairings and settings.

## Important limitations

- **USB Bluetooth adapters are recommended.** Using the PS5's internal radio
  shares hardware with the DualSense controller and can interrupt controller
  input, potentially until a console restart.
- BlueV supports A2DP output (SBC), **not microphone audio**.
- The BlueV web page has **no authentication** and is network-accessible by
  default. Use a trusted LAN or select its **Only this console** access mode.
- This wrapper has host-side packaging tests, but needs validation on real PS5
  hardware and against the specific BlueV release/firmware you install.
- BlueV is GPL-3.0-or-later and is independently maintained by 0xZ3n0x.
  Source, releases, troubleshooting and supported hardware are documented in
  the [upstream BlueV repository](https://github.com/0xZ3n0x/BlueV).

Run the packaging tests on a development host with Python 3.10+, a C compiler
and `llvm-objcopy`:

```sh
python3 -m unittest discover -s scripts/tests -p 'test_package_bluev_plugin.py'
```
