# Pentane (Disney Infinity 3.0 build)

The plugin loader di1compat runs under. This is the Disney Infinity 3.0 Gold Edition build, as bundled with the
Breeze R1.2.5 mod. The upstream project is [high-octane-dev/pentane](https://github.com/high-octane-dev/pentane).

## Files

Copy these into the 3.0 installation folder, next to `DisneyInfinity3.exe`:

```
Pentane.dll
Pentane/config.toml        enables di1compat.dll (enabled_plugins) and windowed mode
Pentane/Plugins/           create it; di1compat.dll and its files go here
```

## How the game loads it

The game executable's first import, `bink2w32.dll`, is renamed to `Pentane.dll` in place (both names fit the same
slot). Windows then loads Pentane at startup, and Pentane forwards the Bink calls to `bink2w32.dll`. Breeze ships
executables already patched this way. On a stock executable, change that one string in the import table, and keep
a backup of the original.
