# 雷达波形生成系统 GUI

基于 PySide6 的雷达波形生成与可视化桌面应用，封装模块01的5种波形模式。

## 目录结构

```
01_GUI_waveform/
├── app.py                          # 程序入口
├── requirements.txt                # Python 依赖
│
├── lib/                            # 构建产物 (.gitignore 已忽略)
│   ├── waveform_cpp.pyd            # C++ 绑定 (Windows, pybind11)
│   ├── waveform_cpp.cpython-314-darwin.so  # C++ 绑定 (macOS, pybind11)
│   ├── libgcc_s_seh-1.dll          # MinGW 运行时 (Windows)
│   ├── libstdc++-6.dll
│   └── libwinpthread-1.dll
│
├── scripts/                        # 构建/打包脚本
│   ├── build.sh                    # macOS 一键构建脚本
│   ├── build.bat                   # Windows 一键构建脚本
│   ├── setup_cython.py             # Cython 编译配置 (备用)
│   ├── 雷达波形生成系统.spec         # PyInstaller macOS 打包配置
│   └── 雷达波形生成系统_win.spec    # PyInstaller Windows 打包配置 (单文件)
│
├── ui/                             # PySide6 界面
│   ├── main_window.py              # 主窗口（含 C++ 调用逻辑、Win32 任务栏图标设置）
│   ├── param_panel.py              # 参数面板（系统参数 + Case 参数）
│   ├── plot_panel.py               # 可视化面板（7种图表，PNG/SVG 导出）
│   ├── console_panel.py            # 控制台日志面板
│   ├── scientific_spinbox.py       # 科学计数法输入框
│   └── theme.py                    # 亮色/暗色主题 (Catppuccin Mocha)
│
├── assets/                         # 资源文件
│   ├── __init__.py
│   ├── app_icon.ico                # Windows 应用图标 (6尺寸, 32bpp ARGB)
│   ├── app_icon.png                # PNG 图标 (256x256)
│   ├── icon_b64.txt                # PNG 图标的 base64 编码 (运行时加载)
│   ├── checkmark.svg               # 勾选图标
│   └── chevron-down.svg            # 下拉箭头图标
│
├── core/                           # Python 后端模块
│   ├── __init__.py
│   ├── config_manager.py           # 配置管理 (config.json 读写)
│   └── signal_utils.py             # 信号处理工具函数
│
├── venv/                           # Python 虚拟环境 (.gitignore 已忽略)
└── dist/                           # 构建输出 (.gitignore 已忽略)
```

## 界面布局

```
┌──────────────────────────────────────────────────────────┐
│  菜单栏: 文件(加载/保存配置) | 视图(主题切换/重置布局) | 帮助  │
├──────────┬───────────────────────────────────────────────┤
│          │           可视化面板                           │
│ 参数面板  │  ┌──────────────────────────────────────┐     │
│          │  │ 时域 | 频域 | 图像 | 频率序列 | ...  │     │
│ 系统参数  │  │                                      │     │
│ fc=16GHz │  │        (7种图表标签页)                │     │
│ B=40MHz  │  │                                      │     │
│ Tp=12us  │  └──────────────────────────────────────┘     │
│ ...      │                                                │
│          │           控制台面板                           │
│ Case参数  │  [INFO] 参数加载完成                           │
│ [Case▼]  │  [SUCCESS] 生成完成                           │
│ N=10     │                                                │
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

01_GUI_waveform 读取项目根目录的 `config.json`，参数修改后点击"运行"即时生效。
也支持通过命令行参数指定配置路径：`python app.py /path/to/config.json`。
配置搜索顺序：环境变量 `SPS_CONFIG` → 向上搜索 config.json（最多10层）→ `~/.config/sps/config.json`。

---

# 构建指南

## macOS 构建

### 前置条件

- macOS + Xcode Command Line Tools（`xcode-select --install`）
- Python 3.13+
- `../third_party/` 存在（解压 `third_party.zip`，包含 eigen）
- **C++ 源码已从仓库移除**，`lib/` 目录中已包含预编译的 `.pyd/.so` 文件，可直接运行 GUI。如需重新编译静态库，需先解压对应的加密 zip 备份（密码见 `docs/BUILD_GUIDE.md`）恢复 C++ 源码。
- 模块01静态库已构建（需先恢复 C++ 源码）：
  ```bash
  cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . --target waveform_core
  ```

### 快速开始

```bash
cd 01_GUI_waveform

# 1. 安装依赖（首次）
bash scripts/build.sh setup

# 2. 开发运行
bash scripts/build.sh run

# 3. 一键构建（C++ + 打包 .app）
bash scripts/build.sh all
```

### macOS 首次运行

由于没有 Apple 开发者签名，首次打开会提示"无法验证开发者"：
1. 右键点击 `.app` → 选择"打开"
2. 或在 系统设置 → 隐私与安全性 → 点击"仍要打开"

---

## Windows 构建

### 前置条件

1. **Python 3.13+** — 从 [python.org](https://www.python.org/downloads/) 下载安装，安装时勾选 "Add Python to PATH"
2. **MinGW-w64 (GCC/G++)** — 推荐 [winlibs.com](https://winlibs.com/) 下载 UCRT runtime 版本，解压后将 `bin` 目录加入 PATH
3. **CMake** — 从 [cmake.org](https://cmake.org/download/) 下载安装
4. **C++ 源码** — 已从仓库移除，`lib/` 目录中已包含预编译的 `.pyd` 文件，可直接运行 GUI。如需重新编译静态库，需先解压对应的加密 zip 备份（密码见 `docs/BUILD_GUIDE.md`）恢复 C++ 源码。

验证环境：
```cmd
python --version          # 应显示 Python 3.13+
g++ --version             # 应显示 GCC 版本
cmake --version           # 应有输出
```

### 一键构建 (推荐)

```cmd
cd 01_GUI_waveform
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
pip install pybind11 pyqtgraph pyinstaller
scripts\build.bat
```

### 分步构建

> **注意**：C++ 源码已从仓库移除，以下步骤需要先解压对应的加密 zip 备份（密码见 `docs/BUILD_GUIDE.md`）恢复 `01_waveform_generation/` 目录。如无需重新编译，可直接使用 `lib/` 中预编译的绑定文件运行 GUI。

**第 1 步：构建模块01静态库**
```cmd
cd 01_waveform_generation
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --config Release --target waveform_core
```

**第 2 步：编译 C++ 绑定模块**
```cmd
cd 01_GUI_waveform
venv\Scripts\activate

REM 获取头文件路径
python -c "import pybind11; print(pybind11.get_include())"
python -c "import sysconfig; print(sysconfig.get_path('include'))"

REM 编译（替换路径为实际值）
g++ -O3 -std=c++17 -shared ^
    -I"<pybind11路径>" ^
    -I"<Python include路径>" ^
    -I"..\third_party\eigen" ^
    -I"..\01_waveform_generation\include" ^
    ..\01_waveform_generation\bindings\waveform_bind.cpp ^
    ..\01_waveform_generation\build\libwaveform_core.a ^
    -o lib\waveform_cpp.pyd
```

**第 3 步：打包 .exe**
```cmd
cd 01_GUI_waveform
venv\Scripts\activate
pyinstaller --clean --noconfirm scripts\雷达波形生成系统_win.spec
```

输出在 `dist\雷达波形生成系统.exe`（单文件模式）。

### 部署

将 `dist\雷达波形生成系统.exe` 单文件拷贝到目标机器即可运行。
- 将 `config.json` 放在 exe 上两级目录
- 或通过菜单 **文件 → 加载配置...** 手动导入
- 或设置环境变量 `SPS_CONFIG` 指向 config.json

---

## 构建流程图

```
01_waveform_generation/
  src/waveform_core.cpp ──── 编译器 ──→ libwaveform_core.a (仅依赖 Eigen)
  bindings/waveform_bind.cpp ─┘             ↕ pybind11
                                    waveform_cpp.pyd / .so (位于 01_GUI_waveform/lib/ 目录)

core/*.py ── 直接 import ──→ 配置管理、工具函数（纯 Python，无需编译）

ui/*.py + assets/ ── PyInstaller ──→ 雷达波形生成系统.app (macOS)
                                    雷达波形生成系统.exe (Windows)
```

## 平台差异

| 项目 | macOS | Windows |
|------|-------|---------|
| C++ 绑定产物 | `waveform_cpp.cpython-314-darwin.so` | `waveform_cpp.pyd` |
| 打包 spec | `雷达波形生成系统.spec` | `雷达波形生成系统_win.spec` |
| 打包输出 | `dist/雷达波形生成系统.app` | `dist/雷达波形生成系统.exe` (单文件, ~90MB) |
| 运行时 DLL | 不需要 | 需要 MinGW UCRT64 (libgcc/libstdc++/libwinpthread) |
| 任务栏图标 | 原生支持 | 需 Win32 API (SetClassLongPtrW + QTimer.singleShot) |
