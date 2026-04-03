# 雷达波形生成系统 GUI

基于 PySide6 的雷达波形生成与可视化桌面应用，封装模块01的5种波形模式。

## 目录结构

```
GUI/
├── app.py                          # 程序入口
├── requirements.txt                # Python 依赖
├── README.md                       # 本文档
│
├── scripts/                        # 构建/打包脚本
│   ├── build.sh                    # macOS 一键构建脚本
│   ├── setup_cython.py             # Cython 编译配置
│   └── 雷达波形生成系统.spec         # PyInstaller 打包配置
│
├── ui/                             # PySide6 界面
│   ├── main_window.py              # 主窗口（含 C++ 调用逻辑 _run_waveform_cpp）
│   ├── param_panel.py              # 参数面板
│   ├── plot_panel.py               # 可视化面板（7种图表）
│   ├── console_panel.py            # 控制台
│   ├── scientific_spinbox.py       # 科学计数法输入框
│   └── theme.py                    # 亮色/暗色主题
│
├── assets/                         # 图标资源
│   ├── checkmark.svg               # 勾选图标
│   └── chevron-down.svg            # 下拉箭头图标
│
├── core/                           # 后端模块（Cython 编译后只有 .so/.pyd）
│   ├── __init__.py
│   ├── config_manager.cpython-*.so
│   └── signal_utils.cpython-*.so
│
├── waveform_cpp.cpython-*.so       # C++ 编译产物（链接 01 模块静态库）
├── venv/                           # Python 虚拟环境
└── dist/                           # 构建输出（.gitignore 已忽略）
    ├── 雷达波形生成系统.app          # macOS
    └── 雷达波形生成系统.exe          # Windows
```

## 界面布局

```
┌──────────────────────────────────────────────────────────┐
│  菜单栏: 文件 | 视图(主题切换) | 帮助                       │
├──────────┬───────────────────────────────────────────────┤
│          │                                                │
│ 参数面板  │           可视化面板                           │
│          │  ┌──────────────────────────────────────┐     │
│ 系统参数  │  │ 时域 | 频域 | 图像 | 频率序列 | ...  │     │
│ fc=16GHz │  │                                      │     │
│ B=40MHz  │  │        (7种图表标签页)                │     │
│ Tp=12us  │  │                                      │     │
│ ...      │  └──────────────────────────────────────┘     │
│          │                                                │
│ Case参数  │           控制台面板                           │
│ [Case▼]  │  [INFO] 参数加载完成                           │
│ N=10     │  [SUCCESS] 生成完成                           │
│ ...      │                                                │
├──────────┴───────────────────────────────────────────────┤
│  状态栏: 就绪 | 进度条                                     │
└──────────────────────────────────────────────────────────┘
```

## 支持的波形模式

| Case | 波形类型 | 关键参数 |
|------|---------|---------|
| 1 | 固定跳频 | 跳频点数N, 频率步进delta_f |
| 2 | 随机相位 | 无额外参数 |
| 3 | PRI抖动 | 标称PRT, 抖动范围jitter_us |
| 4 | 混合(跳频+抖动) | 载频分组数fcnum, PRT, 抖动 |
| 5 | 复合(跳频+随机相位) | 跳频点数N, 频率步进delta_f |

## 配置

GUI 读取项目根目录的 `../config.json`，参数修改后点击"运行"即时生效。

---

# 构建指南

## macOS 构建（一键脚本）

### 前置条件

- macOS + Xcode Command Line Tools（`xcode-select --install`）
- Python 3.13+
- `../third_party/` 存在（包含 eigen）
- 模块01静态库已构建：
  ```bash
  cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . --target waveform_core
  ```

### 快速开始

```bash
cd GUI

# 1. 安装依赖（首次）
bash scripts/build.sh setup

# 2. 开发运行
bash scripts/build.sh run

# 3. 一键构建（C++ + Cython + 打包 .app）
bash scripts/build.sh all
```

### 分步构建

```bash
bash scripts/build.sh cpp      # 编译 C++ 绑定 → waveform_cpp.so（链接 libwaveform_core.a）
bash scripts/build.sh cython   # 编译 core/ 模块为 .so
bash scripts/build.sh app      # 打包 .app
```

### macOS 首次运行

由于没有 Apple 开发者签名，首次打开会提示"无法验证开发者"：
1. 右键点击 `.app` → 选择"打开"
2. 或在 系统设置 → 隐私与安全性 → 点击"仍要打开"

---

## Windows 构建（逐步手动操作）

以下步骤在 Windows 10/11 上按顺序执行即可生成独立的 `雷达波形生成系统.exe`。

### 前置条件

1. **Python 3.13+** — 从 [python.org](https://www.python.org/downloads/) 下载安装，**安装时勾选 "Add Python to PATH"**
2. **MinGW-w64 (GCC/G++)** — 推荐 [winlibs.com](https://winlibs.com/) 下载 UCRT runtime 版本，解压后将 `bin` 目录加入 PATH
3. **CMake** — 从 [cmake.org](https://cmake.org/download/) 下载安装，安装时勾选 "Add CMake to system PATH"
4. **Git**（可选）— 用于克隆项目

验证环境（在 CMD 或 PowerShell 中）：
```cmd
python --version          # 应显示 Python 3.13+
g++ --version             # 应显示 GCC 版本
cmake --version           # 应有输出
```

### 第 1 步：构建模块01静态库

```cmd
cd 01_waveform_generation
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --config Release --target waveform_core
```

成功后生成 `01_waveform_generation/build/libwaveform_core.a`。

> 注意：确保 `third_party/eigen` 目录存在。Eigen 是 header-only，无需编译。

### 第 2 步：创建虚拟环境并安装依赖

```cmd
cd GUI
python -m venv venv
venv\Scripts\activate
pip install --upgrade pip
pip install -r requirements.txt
pip install pybind11 cython pyinstaller
```

验证安装：
```cmd
python -c "import PySide6; print('PySide6 OK')"
python -c "import pyqtgraph; print('pyqtgraph OK')"
python -c "import pybind11; print('pybind11 OK')"
```

### 第 3 步：编译 C++ 绑定模块

```cmd
cd GUI
venv\Scripts\activate
```

获取头文件路径：
```cmd
python -c "import pybind11; print(pybind11.get_include())"
python -c "import sysconfig; print(sysconfig.get_path('include'))"
```

编译（将下面的路径替换为上一步输出的实际路径）：

```cmd
g++ -O3 -std=c++17 -shared ^
    -I"<pybind11路径>" ^
    -I"<Python include路径>" ^
    -I"..\third_party\eigen" ^
    -I"..\01_waveform_generation\include" ^
    ..\01_waveform_generation\bindings\waveform_bind.cpp ^
    ..\01_waveform_generation\build\libwaveform_core.a ^
    -o waveform_cpp.pyd
```

其中 `<pybind11路径>` 和 `<Python include路径>` 替换为上方获取的实际路径。

编译成功后会在 `GUI/` 目录生成 `waveform_cpp.pyd`。

验证：
```cmd
python -c "import waveform_cpp; print('C++ binding OK')"
```

> 如果报 `ImportError: DLL load failed`，可能需要将 MinGW 的 `libstdc++-6.dll` 和 `libgcc_s_seh-1.dll` 复制到 GUI 目录，或在下一步打包时让 PyInstaller 自动收集。

### 第 4 步：编译 Cython 模块

```cmd
cd GUI
venv\Scripts\activate

python scripts\setup_cython.py build_ext --inplace
```

> 需要 `core/config_manager.py` 和 `core/signal_utils.py` 源码存在。如果只有 `.pyd`，则跳过此步。

编译成功后 `core/` 目录会生成 `.pyd` 文件。清理中间文件：
```cmd
del core\config_manager.py core\signal_utils.py core\*.c 2>nul
rmdir /s /q core\__pycache__
```

### 第 5 步：打包 .exe

项目已包含 Windows 专用 spec 文件 `scripts/雷达波形生成系统_win.spec`（自动查找 `.pyd` 文件，无需手动填写文件名）。

```cmd
cd GUI
venv\Scripts\activate
pyinstaller --clean scripts\雷达波形生成系统_win.spec
```

成功后输出在 `dist\雷达波形生成系统\雷达波形生成系统.exe`。

### 第 6 步：部署

将整个 `dist\雷达波形生成系统\` 文件夹拷贝到目标机器即可。运行时需要：

- 将 `config.json` 放在 `雷达波形生成系统.exe` 的**上级目录**（即与 dist 同级的项目根目录）
- 或设置环境变量 `SIGNAL_PROC_CONFIG` 指向 config.json 的完整路径

双击 `雷达波形生成系统.exe` 即可运行，无需安装 Python。

---

## 自动化脚本（Windows）

可将步骤 3~5 整合为 `scripts/build.bat`：

```bat
@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0\.."
set GUI_ROOT=%cd%
set PY=%GUI_ROOT%\venv\Scripts\python.exe

echo === [1/3] 编译 C++ 绑定 ===

for /f "delims=" %%i in ('%PY% -c "import pybind11; print(pybind11.get_include())"') do set PYBIND_INC=%%i
for /f "delims=" %%i in ('%PY% -c "import sysconfig; print(sysconfig.get_path('include'))"') do set PYTHON_INC=%%i

set MODULE01_LIB=%GUI_ROOT%\..\01_waveform_generation\build\libwaveform_core.a
if not exist "%MODULE01_LIB%" (
    echo [错误] 未找到 libwaveform_core.a，请先构建模块01
    exit /b 1
)

g++ -O3 -std=c++17 -shared ^
    -I"%PYBIND_INC%" ^
    -I"%PYTHON_INC%" ^
    -I"%GUI_ROOT%\..\third_party\eigen" ^
    -I"%GUI_ROOT%\..\01_waveform_generation\include" ^
    "%GUI_ROOT%\..\01_waveform_generation\bindings\waveform_bind.cpp" ^
    "%MODULE01_LIB%" ^
    -o waveform_cpp.pyd

echo === [2/3] 编译 Cython 模块 ===
if exist core\config_manager.py (
    %PY% scripts\setup_cython.py build_ext --inplace
    del core\config_manager.py core\signal_utils.py core\*.c 2>nul
    rmdir /s /q core\__pycache__
) else (
    echo [跳过] core/ 已是编译产物
)

echo === [3/3] 打包 .exe ===
if exist build rmdir /s /q build
if exist dist  rmdir /s /q dist
%PY% -m PyInstaller --clean scripts\雷达波形生成系统_win.spec

echo.
echo 完成: dist\雷达波形生成系统\雷达波形生成系统.exe
pause
```

使用方式：双击 `scripts\build.bat` 或在 CMD 中执行。

---

## 构建流程图

```
01_waveform_generation/
  src/waveform_core.cpp ──── 编译器 ──→ libwaveform_core.a/.lib (仅依赖 Eigen)
  bindings/waveform_bind.cpp ─┘             ↕ pybind11
                                    waveform_cpp.so/.pyd (位于 GUI/ 目录)

core/*.py ─────────── Cython ────→ core/*.so/.pyd（配置管理、工具函数）
                                          ↕
ui/*.py + assets/ ── PyInstaller ──→ 雷达波形生成系统.app / .exe
```

## 重新编译 Cython

Cython 编译需要 `.py` 源码，编译后源码会被删除以保护代码。重新编译步骤：

1. 将 `config_manager.py`、`signal_utils.py` 源码放回 `core/` 目录
2. macOS: `bash scripts/build.sh cython` | Windows: `python scripts\setup_cython.py build_ext --inplace`
3. 源码会自动清理，只保留编译产物
