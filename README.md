# Hubble Reference Application — EM Microelectronic EM9305

A Hubble Terrestrial Network beacon for the EM Microelectronics EM9305.

## Requirements

1. EM9305 hardware, e.g. the EMDVK9305SOC.
2. EM9305 SDK v4.6.1 from EM Microelectronic.
3. Synopsys MetaWare for ARCv2EM, with `ccac` on `PATH` or
   `MW_INSTALL_PATH` set. EM offers a free MetaWare-for-EM variant. Tested
   with `X-2025.06 (build 003) (EM-Micro)`.
4. CMake 3.19+, and Ninja or Make.
5. Python 3, and `pip install pyserial`.

> [!WARNING]
> The EM9305 SDK does not have a macOS option. Mac users must use a Linux VM
> and use the Linux SDK.


## Quick start

```sh
git clone https://github.com/HubbleNetwork/hubble-reference-emmicro

cd hubble-reference-emmicro

git submodule update --init --recursive

# Place the EM9305 SDK -- see below. Nothing else to configure.
#   external/EM9305_SDK/
# or, set EM9305_SDK_ROOT environment variable to the SDK.

# Provision your Hubble device key (use -b for base64 key)
python external/hubble-device-sdk/tools/embed_key_time.py -b master.key -o app/hubble/includes

python scripts/hubble_builder.py build
python scripts/hubble_builder.py flash
```


## The EM9305 SDK

The top-level `CMakeLists.txt` builds `app/` with the EM9305 SDK.
`hubble_builder.py build` runs this, or you can use CMake.

```sh
cmake -S . -B build -G Ninja   # or -G "Unix Makefiles"
cmake --build build --target app
```


## Provision the key and the time

 Generate a key on [Hubble Dash](https://dash.hubble.com/dashboard) and paste it here
 into `master.key`. This sample uses a 128-bit, unix time key by default.

```sh
# raw key
python external/hubble-device-sdk/tools/embed_key_time.py master.key -o app/hubble/includes

# base64-encoded key
python external/hubble-device-sdk/tools/embed_key_time.py -b master.key -o app/hubble/includes
```

This writes `app/hubble/includes/key.c` and `time.c`, both gitignored and
untracked so a provisioned key cannot reach a commit. Without this step the
build stops at `#error "No device key or provisioned time."`

**Re-run this before every build.** `time.c` records the moment you ran the
script, and the beacon has no other way to learn the updated time.


## Build and flash

```sh
python scripts/hubble_builder.py build   # configure and build
python scripts/hubble_builder.py flash   # flash a board
```

| Option | Commands | |
|---|---|---|
| `--port COMx` or `--port /dev/ttyACMx` | flash, log | DVK serial port (default `$EM9305_PORT`, then autodetect) |

Flashing and logging use BLEngine, from `PATH` or EM's standalone build under
`<sdk>/tools/blengine/installer`.


## Logging

Works out-of-the-box for the EMDVK9305SOC. Sends messages over UART, formatted
for BLEngine's terminal.

```sh
python scripts/hubble_builder.py log
```

Change `HUBBLE_LOG_MIN_LEVEL` or define `CONFIG_HUBBLE_LOG_LEVEL_OFF` to tune logging
verbosity or disable it.


## Validation

You can validate with a Python tool in the Hubble SDK, or with the Hubble Connect app.

### Python tool

Use the SDK's `scan.py` tool:
1. Install requirements `pip install -r external/hubble-device-sdk/tools/requirements-scan.txt`.
2. Run `python external/hubble-device-sdk/tools/scan.py -k "<Hubble Key>"`.
3. You should see packets appear in your terminal.

### Hubble Connect app

Use the Hubble Connect app while signed in to your account. Your device should appear in "My Devices"
if functioning properly.


## License

This application is Apache-2.0; see [LICENSE](LICENSE).
