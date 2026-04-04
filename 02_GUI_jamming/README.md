# 雷达干扰生成系统 GUI

基于 PySide6 的雷达干扰生成与可视化桌面应用，封装模块02的10种干扰模式。

## 目录结构

```
02_GUI_jamming/
├── app.py                          # 程序入口
├── requirements.txt                # Python 依赖
│
├── lib/                            # 构建产物 (.gitignore 已忽略)
│   ├── jamming_cpp.pyd             # C++ 绑定 (Windows, pybind11)
│   ├── libgcc_s_seh-1.dll          # MinGW 运行时 (Windows)
│   ├── libstdc++-6.dll
│   └── libwinpthread-1.dll
│
├── scripts/                        # 构建/打包脚本
│   ├── build.sh                    # macOS 一键构建脚本
│   ├── build.bat                   # Windows 一键构建脚本
│   ├── setup_cython.py             # Cython 编译配置 (备用)
│   ├── 雷达干扰生成系统.spec         # PyInstaller macOS 打包配置
│   └── 雷达干扰生成系统_win.spec    # PyInstaller Windows 打包配置
│
├── ui/                             # PySide6 界面
│   ├── main_window.py              # 主窗口（含 C++ 调用逻辑、Win32 任务栏图标设置）
│   ├── param_panel.py              # 参数面板（系统参数 + 10种Case参数）
│   ├── plot_panel.py               # 增强版可视化面板（7种图表，PNG/SVG导出）
│   ├── console_panel.py            # 控制台日志面板
│   ├── scientific_spinbox.py       # 科学计数法输入框
│   └── theme.py                    # 亮色/暗色主题 (Catppuccin Mocha)
│
├── assets/                         # 资源文件
│   ├── __init__.py
│   ├── app_icon.ico                # Windows 应用图标 (含右下角红色 "02" 徽章)
│   ├── app_icon.png                # PNG 图标 (256x256)
│   ├── icon_b64.txt                # PNG 图标的 base64 编码 (运行时加载)
│   ├── checkmark.svg               # 勾选图标
│   └── chevron-down.svg            # 下拉箭头图标
│
├── core/                           # Python 后端模块
│   ├── __init__.py
│   ├── config_manager.py           # 配置管理 (config.json jamming 节读写)
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
│          │  │ 时域 | 频域 | 矩阵 | RD图 | STFT | ... │     │
│ 系统参数  │  │                                      │     │
│ fc=16GHz │  │        (7种图表标签页)                │     │
│ B=40MHz  │  │                                      │     │
│ Tp=12us  │  └──────────────────────────────────────┘     │
│ ...      │                                                │
│          │           控制台面板                           │
│ Case参数  │  [INFO] 参数加载完成                           │
│ [Case▼]  │  [SUCCESS] 生成完成                           │
│ Rj=100m  │                                                │
│ ...      │                                                │
├──────────┴───────────────────────────────────────────────┤
│  状态栏: 就绪 | 进度条                                     │
└──────────────────────────────────────────────────────────┘
```

## 支持的干扰模式

| Case | 干扰类型 | 缩写 | 关键参数 |
|------|---------|------|---------|
| 1 | 距离假目标 | RDJ | 延迟脉冲数jj, 假目标距离Rj, 幅度增益amp_j |
| 2 | 速度假目标 | VDJ | 假目标速度Vj, 延迟脉冲数jj, 幅度增益amp_j |
| 3 | 间歇采样转发 | ISRJ | 采样周期Ts_ISRJ, 采样脉宽T_ISRJ, 幅度增益amp_j |
| 4 | 窄带噪声 | NNJ | 噪声功率power_dBW, 滤波器阶数, 截止频率 |
| 5 | 距离波门拖引 | RGPO | 拖引速度Vj, 干扰幅度, 拖引阶段数drag_stages |
| 6 | 速度波门拖引 | VGPO | 拖引速度Vj, 干扰幅度, 拖引阶段数drag_stages |
| 7 | 密集复制转发假目标 | DRFTJ | 干信比JSR, 转发次数num_jam, 距离增量detaR |
| 8 | 脉内前沿切片重复 | IPLESRJ | 功率增益A_RJ, 初始距离R0_new, 切片比T_ISRJ_ratio |
| 9 | 频谱弥散 | SMSP | 切片数num_slices, 干信比JSR, 额外系数amp_extra |
| 10 | 梳状谱 | COMB | 谱线数num_tones, 干信比JSR, 频率间隔deltaf |

## 可视化图表 (7种)

| 标签页 | 说明 | 数据源 |
|--------|------|--------|
| 时域波形 | 目标/干扰/合成信号 (实部/虚部/包络) | 单脉冲列 |
| 频域频谱 | FFT 幅度谱 (dB) | 单脉冲 FFT |
| 信号矩阵 | 热力图 (实部/虚部/幅度/相位) | 完整矩阵 (nrn x nan1) |
| 距离-多普勒 | 2D RD 图 (range-FFT + Doppler-FFT) | 去斜 + 2D-FFT |
| STFT 时频 | 时频分析图 (去载波后) | 单脉冲 STFT |
| 脉冲对比 | 目标/干扰/合成三线叠加对比 | 单脉冲列 |
| 拖引轨迹 | 峰值距离/速度 vs 脉冲索引 (仅 Case 5/6) | 全矩阵逐脉冲峰值 |

## 配置

02_GUI_jamming 读取项目根目录的 `../config.json`，使用 `system.*` 和 `jamming.*` 配置节。
参数修改后点击"运行"即时生效。
也支持通过命令行参数指定配置路径：`python app.py /path/to/config.json`。

---

# 构建指南

## macOS 构建

### 前置条件

- macOS + Xcode Command Line Tools（`xcode-select --install`）
- Python 3.13+
- `../third_party/` 存在（包含 eigen、fftw-install）
- **C++ 源码已从仓库移除**，`lib/` 目录中已包含预编译的 `.pyd/.so` 文件，可直接运行 GUI。如需重新编译静态库，需先解压对应的加密 zip 备份（密码见 `docs/BUILD_GUIDE.md`）恢复 C++ 源码。
- 模块02静态库已构建（需先恢复 C++ 源码）：
  ```bash
  cd 02_jamming_generation && mkdir -p build && cd build && cmake .. && cmake --build . --target jamming_core
  ```

### 快速开始

```bash
cd 02_GUI_jamming

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
cd 02_GUI_jamming
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
pip install pybind11 pyqtgraph pyinstaller
scripts\build.bat
```

### 分步构建

> **注意**：C++ 源码已从仓库移除，以下步骤需要先解压对应的加密 zip 备份（密码见 `docs/BUILD_GUIDE.md`）恢复 `02_jamming_generation/` 目录。如无需重新编译，可直接使用 `lib/` 中预编译的绑定文件运行 GUI。

**第 1 步：构建模块02静态库**
```cmd
cd 02_jamming_generation
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --config Release --target jamming_core
```

**第 2 步：编译 C++ 绑定模块**
```cmd
cd 02_GUI_jamming
venv\Scripts\activate

REM 获取头文件路径
python -c "import pybind11; print(pybind11.get_include())"
python -c "import sysconfig; print(sysconfig.get_path('include'))"

REM 编译（替换路径为实际值）
g++ -O3 -std=c++17 -shared ^
    -I"<pybind11路径>" ^
    -I"<Python include路径>" ^
    -I"..\third_party\eigen" ^
    -I"..\third_party\fftw-install\include" ^
    -I"..\02_jamming_generation\include" ^
    ..\02_jamming_generation\bindings\jamming_bind.cpp ^
    ..\02_jamming_generation\build\libjamming_core.a ^
    -L"..\third_party\fftw-install\lib" -lfftw3 ^
    -o lib\jamming_cpp.pyd
```

**第 3 步：打包 .exe**
```cmd
cd 02_GUI_jamming
venv\Scripts\activate
pyinstaller --clean --noconfirm scripts\雷达干扰生成系统_win.spec
```

输出在 `dist\雷达干扰生成系统\雷达干扰生成系统.exe`。

### 部署

将整个 `dist\雷达干扰生成系统\` 文件夹拷贝到目标机器即可。
- 将 `config.json` 放在 exe 的**上级目录**
- 或设置环境变量 `SPS_CONFIG` 指向 config.json

---

## 构建流程图

```
02_jamming_generation/
  src/jamming_core.cpp ──── 编译器 ──→ libjamming_core.a (依赖 Eigen + FFTW3)
  bindings/jamming_bind.cpp ─┘             ↕ pybind11
                                    jamming_cpp.pyd / .so (位于 02_GUI_jamming/lib/ 目录)

core/*.py ── 直接 import ──→ 配置管理、工具函数（纯 Python，无需编译）

ui/*.py + assets/ ── PyInstaller ──→ 雷达干扰生成系统.app (macOS)
                                    雷达干扰生成系统.exe (Windows)
```

## 平台差异

| 项目 | macOS | Windows |
|------|-------|---------|
| C++ 绑定产物 | `jamming_cpp.cpython-314-darwin.so` | `jamming_cpp.pyd` |
| 打包 spec | `雷达干扰生成系统.spec` | `雷达干扰生成系统_win.spec` |
| 打包输出 | `dist/雷达干扰生成系统.app` | `dist/雷达干扰生成系统.exe` (单文件) |
| 运行时 DLL | 不需要 | 需要 MinGW UCRT64 (libgcc/libstdc++/libwinpthread) + FFTW3 |
| 任务栏图标 | 原生支持 | 需 Win32 API (SetClassLongPtrW + QTimer.singleShot) |
