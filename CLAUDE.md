# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Radar signal processing research system (雷达信号处理系统) — C++17 core with PySide6 GUIs. Implements four independent modules: waveform generation, jamming simulation, jamming detection/suppression, and signal processing algorithms.

**Note**: C++ source code folders have been removed from the repository. Encrypted zip backups are available in the project root (see `docs/BUILD_GUIDE.md` for password). The GUI folders contain pre-built `.pyd`/`.so` bindings in `lib/`.

## Build & Run

### GUI (Python) — primary interface

```bash
cd 01_GUI_waveform && pip install -r requirements.txt && python app.py
cd 02_GUI_jamming && pip install -r requirements.txt && python app.py
cd 03_GUI_detection && pip install -r requirements.txt && python app.py
cd 04_GUI_signal_processing && pip install -r requirements.txt && python app.py
```

### C++ modules — requires extracting from encrypted zip

See `docs/BUILD_GUIDE.md` for full C++ build instructions. C++ source is archived in:
- `01_waveform_generation.zip`, `02_jamming_generation.zip`
- `03_jamming_detection_suppression.zip`, `04_signal_processing.zip`

### Windows exe packaging

Each GUI has a PyInstaller spec for single-file exe output (located in `scripts/` subdirectory):
```bash
cd 01_GUI_waveform && venv\Scripts\activate && pyinstaller --clean scripts\雷达波形生成系统_win.spec
```
Output: `dist/雷达波形生成系统.exe` (single file, ~90MB).

Alternatively, build all 4 GUIs at once from the project root:
```bash
build_all.bat
```

## Architecture

```
01_GUI_waveform/              → PySide6 app → waveform_cpp.pyd (libwaveform_core.a)
02_GUI_jamming/               → PySide6 app → jamming_cpp.pyd (libjamming_core.a)
03_GUI_detection/             → PySide6 app → detection_cpp.pyd (libdetection_core.a)
04_GUI_signal_processing/     → PySide6 app → signal_processing_cpp.pyd (libsignal_processing_core.a + libwaveform_core.a)
```

### Static Libraries for GUI

Each GUI links a C++ static library via pybind11. The `.pyd`/`.so` bindings in `lib/` are pre-built.

- **01_GUI_waveform** links `libwaveform_core.a` (5 waveform generation functions). Binding: `waveform_bind.cpp`. Requires Eigen.
- **02_GUI_jamming** links `libjamming_core.a` (10 jamming generation functions). Binding: `jamming_bind.cpp`. Requires Eigen + FFTW3.
- **03_GUI_detection** links `libdetection_core.a` (detection+separation pipeline). Binding: `detection_bind.cpp`. Requires Eigen + FFTW3. Config injection via temp JSON file.
- **04_GUI_signal_processing** links `libsignal_processing_core.a` + `libwaveform_core.a`. Binding: `signal_processing_bind.cpp`. Requires Eigen + FFTW3. Config injection via temp JSON file. Three entry points: `run_recognition()`, `run_processing_rd()`, `run_processing_decouple()`.

### Inter-Module Data Flow (C++ level)

```
Module 01 (outputs 11 files) ──→ Module 04 (Case1~5 load matching waveforms)
Module 02 (outputs 20 files) ──→ Module 04 (shibie + Case6 load jammed data)
Module 03 (outputs 6 files)  ←→  Completely independent (no file exchange)
01_GUI_waveform ──pybind11──→ Module 01 only (in-memory numpy arrays, no .dat file I/O)
02_GUI_jamming  ──pybind11──→ Module 02 only (in-memory numpy arrays, no .dat file I/O)
03_GUI_detection ──pybind11──→ Module 03 only (in-memory numpy arrays, temp JSON for config injection)
04_GUI_signal_processing ──pybind11──→ Module 04 + Module 01 (in-memory numpy arrays, temp JSON for config injection)
```

- Module 04 is the sole data consumer. `load_case_data(mode)` loads per-Case matching data from modules 01/02.
- Module 02 Cases 1-3 (repeater-type: RDJ, VDJ, ISRJ) internally generate target echoes instead of loading from Module 01. Cases 4-10 (generative-type) are self-contained.
- Recommended run order: 01 → 02 → 04. Module 03 is independent.
- **Config system**: `Config.h` singleton loads `config.json` at runtime. Supports `//` and `#` comments, trailing commas, null-safe access with defaults. File search order: env var `SPS_CONFIG` → current dir → parent dir → `~/.config/sps/config.json`.
- **Module 03 is independent**: Uses its own parameter set (fc=35GHz Ka-band, B=80MHz, R0=1000m, nrn=2048) separate from modules 01/02/04 (fc=16GHz). Parameters are in the `detection_suppression.*` section of config.json.
- **Utility layer**: `Module0.h` (in each module) provides shared FFT/IFFT, filtering, window functions. `parameters.h` defines system parameter accessors.
- **Output**: All modules write to `output/` — `.dat` (binary with row/col header + complex pairs) and `.txt` files. Naming: `{module}_{Case|type}_{description}.{dat|txt}`.

## Key Dependencies

Located in `third_party.zip` (extract to `third_party/` before building): Eigen 3.4.0 (header-only), FFTW 3.3.10 (source + compiled), mini JSON parser (`nlohmann/json.hpp`, ~215 lines, drop-in replacement). Library detection in `cmake/FindLibraries.cmake`: `third_party/xxx-install/` → `/opt/homebrew/` → `/usr/local/` → env vars.

### Replaced Dependencies

- **Boost** (was `boost_math_tr1` for `cyl_bessel_i`) → Self-implemented `bessel_i0()` Taylor series in `Module0.h` (verified to machine epsilon accuracy for x ∈ [0, 40])
- **nlohmann/json** (was 24765 lines) → Mini JSON parser (~215 lines) at `third_party/nlohmann/json.hpp` (in `third_party.zip`), maintaining `nlohmann::json` namespace API compatibility

## Code Conventions

- All signal processing logic lives in header files (`include/*.h`) with inline/template implementations. `src/` contains only `main.cpp` entry points and module-specific implementations.
- Module 01's `waveform_core.cpp` is the exception — extracted shared code for GUI reuse.
- Module 03's `detection_core.cpp` is another exception — thin wrapper that assembles the detection+separation pipeline for GUI.
- Module 04's `signal_processing_core.cpp` is similar — thin wrapper providing three entry points for GUI.
- Chinese comments are used extensively alongside English — preserve bilingual style when editing.
- Module parameter headers (`parameters.h`) are duplicated across modules 01/02/04 rather than shared — maintain consistency when modifying.

## GUI Platform Notes

- **Build artifacts**: Each `XX_GUI_xxx/lib/` contains the `.pyd`/.so` binding and MinGW DLLs (Windows). `app.py` registers `lib/` in `sys.path` at startup. All `lib/` directories are `.gitignore`d.
- **Windows exe**: Single-file mode via PyInstaller. `config.json` is auto-discovered 2 levels above the exe directory. Can also be loaded via **文件 → 加载配置...** menu.
- **Windows 11 taskbar icon**: Qt's native `setWindowIcon` is insufficient. The fix requires three elements together: (1) `SetCurrentProcessExplicitAppUserModelID` before window creation, (2) `SetClassLongPtrW(GCLP_HICONSM/HICON)` for class-level icon persistence, (3) `QTimer.singleShot(200, ...)` deferred call in `showEvent` to bypass Qt's internal icon reset. See `ui/main_window.py:_apply_win32_taskbar_icon()`.
- **SVG export bug**: pyqtgraph 0.14.0 `SVGExporter` crashes on space-separated path coords. Patched in `venv/Lib/site-packages/pyqtgraph/exporters/SVGExporter.py`.
- **Icon loading**: `assets/icon_b64.txt` stores base64-encoded PNG, loaded at runtime. `assets/app_icon.ico` used for Win32 API and PyInstaller exe icon.
- **PyInstaller excludes**: Only exclude clearly unnecessary packages (`tkinter`, `IPython`, `jupyter`, `notebook`, `matplotlib`, `pandas`, `flask`, `django`, `pytest`, `lib2to3`, `setuptools`, `pip`). Do NOT exclude `unittest`, `pydoc`, `multiprocessing`, `inspect`, `xml` etc. — they are needed by numpy/scipy/pyqtgraph dependency chains.
- **PyInstaller spec files**: Located in each GUI's `scripts/` directory with Chinese names (`*_win.spec`). Modules 02/03/04 also have `build_win.spec` (directory mode, legacy) and `build_ascii.bat` — these are superseded by the Chinese-named single-file specs.
- **Python version**: GUIs use Python 3.14 (venv). C++ pybind11 bindings target `cpython-314`.
