# Light Engine DLL

Dynamically loadable light rendering engine for the Light Studio JUCE plugin.

## Building

### Windows (Visual Studio)
```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### macOS/Linux
```bash
cmake -B build
cmake --build build
```

## Output

- Windows: `LightEngine.dll`
- macOS: `LightEngine.dylib`
- Linux: `LightEngine.so`

The DLL is automatically copied to `../DRLightStudio/Light Studio/Resources/` after building.

## Architecture

- `src/LightEngineAPI.cpp` - API entry points and dispatcher
- `src/Metadata.cpp` - Parameter and mode metadata
- `src/Constants.cpp` - Shared lookup tables (COLOR_PHASE, etc.)
- `src/Modes/` - Individual mode rendering functions

## Syncing with Arduino

The rendering functions in `src/Modes/` should match the corresponding code in `lights.ino`.
Update both when adding or modifying modes.
