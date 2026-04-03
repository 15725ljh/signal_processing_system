# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Radar signal processing research system (雷达信号处理系统) — C++17 with a PySide6 GUI. Implements four independent modules: waveform generation, jamming simulation, jamming detection/suppression, and signal processing algorithms.

## Build & Run

Each module builds independently with its own `build/` directory:

```bash
# Build single module (standalone)
cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd 02_jamming_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd 03_jamming_detection_suppression && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd 04_signal_processing && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# Or build all from project root
mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# Run (from project root)
./build/01_waveform_generation/waveform_gen
./build/02_jamming_generation/jamming_gen
./build/03_jamming_detection_suppression/jamming_det_sup
./build/04_signal_processing/signal_proc

# GUI (Python)
cd GUI_waveform && pip install -r requirements.txt && python app.py
```

## Architecture

Four independent C++ modules, sharing a JSON config system. Each module has its own `src/main.cpp` entry point and builds to a standalone executable. Module 01 additionally builds a static library (`libwaveform_core.a`) shared with the GUI.

```
01_waveform_generation/    → waveform_gen + libwaveform_core.a (5 waveform modes)
02_jamming_generation/     → jamming_gen    (10 jamming types)
03_jamming_detection_suppression/ → jamming_det_sup (5 detection types, fc=35GHz independent)
04_signal_processing/      → signal_proc    (6 processing algorithms + jamming recognition)
GUI_waveform/              → PySide6 app → links libwaveform_core.a via pybind11
GUI_jamming/               → PySide6 app → links libjamming_core.a via pybind11
```

### Module 01 Static Library

Module 01 builds `libwaveform_core.a` containing the 5 waveform generation functions (`waveform_core.cpp`). The GUI's `waveform_cpp.so` (pybind11 binding at `01_waveform_generation/bindings/waveform_bind.cpp`) links against this static library, avoiding code duplication.

### Inter-Module Data Flow

```
Module 01 (outputs 11 files) ──→ Module 04 (Case1~5 load matching waveforms)
Module 02 (outputs 20 files) ──→ Module 04 (shibie + Case6 load jammed data)
Module 03 (outputs 6 files)  ←→  Completely independent (no file exchange)
GUI_waveform ──pybind11──→ Module 01 only (in-memory numpy arrays, no .dat file I/O)
GUI_jamming  ──pybind11──→ Module 02 only (in-memory numpy arrays, no .dat file I/O)
```

- Module 04 is the sole data consumer. `load_case_data(mode)` loads per-Case matching data from modules 01/02.
- Module 02 Cases 1-3 (repeater-type: RDJ, VDJ, ISRJ) internally generate target echoes instead of loading from Module 01. Cases 4-10 (generative-type) are self-contained.
- Recommended run order: 01 → 02 → 04. Module 03 is independent.
- **Config system**: `Config.h` singleton loads `config.json` at runtime. Supports `//` and `#` comments, trailing commas, null-safe access with defaults. File search order: env var `SIGNAL_PROC_CONFIG` → current dir → parent dir → `~/.signal_processing/config.json`.
- **Module 03 is independent**: Uses its own parameter set (fc=35GHz Ka-band, B=80MHz, R0=1000m, nrn=2048) separate from modules 01/02/04 (fc=16GHz). Parameters are in the `detection_suppression.*` section of config.json.
- **Utility layer**: `Module0.h` (in each module) provides shared FFT/IFFT, filtering, window functions. `parameters.h` defines system parameter accessors.
- **Output**: All modules write to `output/` — `.dat` (binary with row/col header + complex pairs) and `.txt` files. Naming: `{module}_{Case|type}_{description}.{dat|txt}`.

## Key Dependencies

Located in `third_party/`: Eigen 3.4.0 (header-only), FFTW 3.3.10 (compiled), mini JSON parser (`nlohmann/json.hpp`, ~215 lines, drop-in replacement). Library detection in `cmake/FindLibraries.cmake`: `third_party/xxx-install/` → `/opt/homebrew/` → `/usr/local/` → env vars.

### Replaced Dependencies

- **Boost** (was `boost_math_tr1` for `cyl_bessel_i`) → Self-implemented `bessel_i0()` Taylor series in `Module0.h` (verified to machine epsilon accuracy for x ∈ [0, 40])
- **nlohmann/json** (was 24765 lines) → Mini JSON parser (~215 lines) at `third_party/nlohmann/json.hpp`, maintaining `nlohmann::json` namespace API compatibility

## Code Conventions

- All signal processing logic lives in header files (`include/*.h`) with inline/template implementations. `src/` contains only `main.cpp` entry points and module-specific implementations.
- Module 01's `waveform_core.cpp` is the exception — extracted shared code for GUI reuse.
- Chinese comments are used extensively alongside English — preserve bilingual style when editing.
- Module parameter headers (`parameters.h`) are duplicated across modules 01/02/04 rather than shared — maintain consistency when modifying.

## GUI Platform Notes

- **Build artifacts**: `GUI_waveform/lib/` contains `waveform_cpp.pyd/.so` and MinGW DLLs. `GUI_jamming/lib/` contains `jamming_cpp.pyd` and MinGW DLLs. `app.py` registers `lib/` in `sys.path` at startup. Both `lib/` directories are `.gitignore`d.
- **Windows 11 taskbar icon**: Qt's native `setWindowIcon` is insufficient. The fix requires three elements together: (1) `SetCurrentProcessExplicitAppUserModelID` before window creation, (2) `SetClassLongPtrW(GCLP_HICONSM/HICON)` for class-level icon persistence, (3) `QTimer.singleShot(200, ...)` deferred call in `showEvent` to bypass Qt's internal icon reset. See `ui/main_window.py:_apply_win32_taskbar_icon()`.
- **SVG export bug**: pyqtgraph 0.14.0 `SVGExporter` crashes on space-separated path coords. Patched in `venv/Lib/site-packages/pyqtgraph/exporters/SVGExporter.py`.
- **Icon loading**: `assets/icon_b64.txt` stores base64-encoded PNG, loaded at runtime. `assets/app_icon.ico` used for Win32 API and PyInstaller exe icon.
