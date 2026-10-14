# Pentane (Disney Infinity 3.0 build)

The plugin loader di1compat runs under. This is the Disney Infinity 3.0 Gold Edition build, as bundled with the
Breeze R1.2.5 mod. The upstream project is [high-octane-dev/pentane](https://github.com/high-octane-dev/pentane).

## Installing

From the repository root:

```powershell
python pentane_install.py              # finds the game through Steam; --game <folder> to name it
python pentane_install.py --dry        # show what it would change
python pentane_install.py --revert     # put the original executable back
```

The script:
- patches the game executable so it loads Pentane, keeping the original as `DisneyInfinity3.exe.prepentane`;
- copies `Pentane.dll` and `Pentane\config.toml`, keeping an existing config;
- creates `Pentane\Plugins\`;
- makes sure `di1compat.dll` is enabled.

Steam restores the original executable when it updates or verifies the game files. Run the script again after that.

How the patch works: the executable's first import, `bink2w32.dll`, is renamed to `Pentane.dll` in place. Windows
then loads Pentane at startup, and Pentane forwards the Bink calls to `bink2w32.dll`. That rename is the only
difference between Breeze's stock and patched executables, and `--selftest` checks that the script reproduces it
byte for byte.

## Configuring

Pentane reads `Pentane\config.toml` in the game folder:

| Key | Section | Meaning |
|---|---|---|
| `enable_plugins` | `[config]` | Load the DLLs in `Pentane\Plugins\`. Must be `true` |
| `enable_file_logging` | `[config]` | Write `pentane.log` next to the executable |
| `enable_console_logging` | `[config]` | Open a console window with Pentane's log |
| `language` | `[config]` | Language of Pentane's own messages |
| `enable_windowed_mode` | `[game-config]` | Run the game in a window instead of full screen |
| `windowed_mode_width`, `windowed_mode_height` | `[game-config]` | Window size |
| `enabled_plugins` | `[plugins]` | The plugins to load, highest priority first. Must include `"di1compat.dll"` |

di1compat's own settings are in `Pentane\Plugins\di1compat.cfg` (see the main README).
