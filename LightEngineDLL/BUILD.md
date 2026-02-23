# Light Engine DLL - Build Instructions

## Prerequisites

### Windows
- **CMake**: Download from https://cmake.org/download/ (3.15 or higher)
- **Visual Studio**: 2019 or newer with "Desktop development with C++" workload
- **Alternative**: MinGW-w64 for GCC-based builds

### macOS
```bash
brew install cmake
# Xcode Command Line Tools (auto-prompted by cmake)
xcode-select --install
```

### Linux
```bash
# Ubuntu/Debian
sudo apt install cmake build-essential

# Fedora
sudo dnf install cmake gcc-c++
```

---

## Building the DLL

### Quick Build (Windows PowerShell)

```powershell
cd "c:\Users\sandm\OneDrive\Documents\Light Studio\LightEngineDLL"

# Configure (generates Visual Studio solution)
cmake -B build

# Build (compiles DLL)
cmake --build build --config Release

# Output: ../DRLightStudio/Light Studio/Resources/LightEngine.dll
```

### Quick Build (macOS/Linux)

```bash
cd "/Users/yourname/Documents/Light Studio/LightEngineDLL"

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Output: ../DRLightStudio/Light Studio/Resources/LightEngine.dylib (or .so)
```

---

## Build Targets

### Debug Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
- Includes debug symbols
- No optimization
- Easier debugging with IDE

### Release Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
- Optimized for performance
- Smaller file size
- Use for production

### Clean Build
```bash
# Remove build directory and rebuild
rm -rf build
cmake -B build
cmake --build build
```

---

## Troubleshooting

### CMake not found
**Windows**: Add CMake to PATH during installation, or use full path:
```powershell
& "C:\Program Files\CMake\bin\cmake.exe" -B build
```

**macOS/Linux**: Ensure cmake is in PATH:
```bash
which cmake  # Should print /usr/local/bin/cmake or similar
```

### Missing Compiler

**Windows**: Install Visual Studio with C++ workload
- Visual Studio 2022 Community (free): https://visualstudio.microsoft.com/

**macOS**: Install Xcode Command Line Tools:
```bash
xcode-select --install
```

**Linux**: Install GCC:
```bash
sudo apt install build-essential  # Ubuntu
```

### Build Errors

**Error: Cannot find LightEngineAPI.h**
- Ensure `Shared/LightEngineAPI.h` exists
- Check relative path: `../Shared/LightEngineAPI.h` from CMakeLists.txt

**Error: Multiple definitions**
- Check that constants are marked `extern` in headers
- Ensure only one .cpp file defines each constant

**Linker Errors**
- Verify all mode functions are implemented
- Check for typos in function names (case-sensitive)

---

## Verification

### Test DLL Load (Windows)

```powershell
# Use Dependency Walker or check exports
dumpbin /EXPORTS build\Release\LightEngine.dll
```

Expected exports:
- `getParameterCount`
- `getParameterName`
- `getForegroundModeCount`
- `renderForeground`
- `renderBackground`
- `exportTemplateCSV`

### Test DLL Load (macOS/Linux)

```bash
# Check exports
nm -g build/LightEngine.dylib | grep render
```

### Size Check

Expected DLL size: ~50-200 KB (depending on platform/optimization)

---

## Integration with JUCE Plugin

After successful build:

1. **Verify output location**:
   ```
   DRLightStudio/Light Studio/Resources/LightEngine.dll
   ```

2. **JUCE plugin will load DLL at runtime**:
   - Uses `juce::DynamicLibrary` class
   - Loads from Resources/ folder relative to plugin binary
   - Falls back to system search paths

3. **Next steps**:
   - Implement `LightEngineLoader.h` in JUCE plugin (see IMPLEMENTATION.md)
   - Create `LightPreviewComponent` for UI
   - Test in FL Studio or standalone plugin host

---

## Development Workflow

### Iterative Development

1. **Modify mode** (e.g., `src/Modes/RainbowWheel.cpp`)
2. **Rebuild DLL**: `cmake --build build`
3. **Reload DLL in plugin** (hot-swap without restarting DAW)
4. **Test visually** in plugin preview or on hardware

### Adding New Modes

1. Create `src/Modes/NewMode.cpp`
2. Add to `CMakeLists.txt` → `MODE_SOURCES`
3. Add metadata to `src/Metadata.cpp` → `FG_MODES` or `BG_MODES`
4. Add case to `src/LightEngineAPI.cpp` → `renderForeground` dispatcher
5. Rebuild

---

## Advanced Options

### Custom Output Directory

```bash
cmake -B build -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="/custom/path"
```

### Cross-Compilation

For building macOS .dylib on Windows (requires special toolchain):
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=osxcross-toolchain.cmake
```

### Static Analysis

Enable compiler warnings:
```bash
cmake -B build -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"
cmake --build build
```

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Build Light Engine DLL

on: [push, pull_request]

jobs:
  build:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [windows-latest, macos-latest, ubuntu-latest]
    
    steps:
    - uses: actions/checkout@v3
    
    - name: Configure
      run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      working-directory: LightEngineDLL
    
    - name: Build
      run: cmake --build build --config Release
      working-directory: LightEngineDLL
    
    - name: Upload DLL
      uses: actions/upload-artifact@v3
      with:
        name: light-engine-${{ matrix.os }}
        path: DRLightStudio/Light Studio/Resources/LightEngine.*
```

---

## Performance Profiling

### Measure Rendering Speed

```cpp
#include <chrono>

auto start = std::chrono::high_resolution_clock::now();
renderForeground(modeId, leds, 108, &ctx);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

// Target: < 100 microseconds for 108 LEDs at 30 FPS
```

### Optimization Tips

- Use Release build (`-O3` optimization)
- Avoid dynamic allocation in render loop
- Prefer integer math over floating point
- Use lookup tables (e.g., `COLOR_PHASE`) instead of `sin()`

---

## Licensing

Ensure all dependencies and mode algorithms are appropriately licensed.

For FastLED compatibility:
- HSV struct is binary-compatible with FastLED's `CHSV`
- RGB conversion algorithms match FastLED's implementation
- No FastLED code directly included (clean-room implementation)
