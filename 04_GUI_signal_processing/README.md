# 雷达信号处理系统 GUI

基于 PySide6 的雷达信号处理可视化桌面应用，封装模块04的6种处理算法，同时链接模块01的波形生成引擎。

## 目录结构

```
04_GUI_signal_processing/
├── app.py                          # 程序入口
├── requirements.txt                # Python 依赖
│
├── lib/                            # 构建产物 (.gitignore 已忽略)
│   ├── signal_processing_cpp.pyd   # C++ 绑定 (Windows, pybind11)
│   ├── libgcc_s_seh-1.dll          # MinGW 运行时 (Windows)
│   ├── libstdc++-6.dll
│   └── libwinpthread-1.dll
│
├── scripts/                        # 构建/打包脚本
│   ├── build.sh                    # macOS 一键构建脚本
│   ├── build.bat                   # Windows 一键构建脚本
│   ├── setup_cython.py             # Cython 编译配置 (备用)
│   ├── 雷达信号处理系统.spec         # PyInstaller macOS 打包配置
│   ├── 雷达信号处理系统_win.spec    # PyInstaller Windows 打包配置 (单文件)
│   ├── build_win.spec               # PyInstaller 目录模式配置 (旧版)
│   └── build_ascii.bat              # ASCII 编码构建脚本 (旧版)
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
│   ├── app_icon.ico                # Windows 应用图标 (含右下角紫色 "04" 徽章)
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

## 支持的处理模式

### RD 处理 (Case 1~5)

| Case | 处理类型 | 说明 |
|------|---------|------|
| 1 | 跳频信号处理 | 加载模块01的跳频波形数据 |
| 2 | 固定载频处理 | 加载模块01的固定载频波形数据 |
| 3 | 传统脉冲压缩 | 加载模块01的 LFM 波形数据 |
| 4 | 改进型脉冲压缩 | 加载模块01的 NLFM 波形数据 |
| 5 | 复合处理 | 加载模块01的复合波形数据 |

### 解耦处理 (Case 6)

加载模块02的含干扰回波数据，支持以下干扰类型：

| 干扰类型 | 缩写 |
|---------|------|
| 间歇采样直接转发 | ISDJ |
| 间歇采样重复转发 | ISRJ |
| 间歇采样循环转发 | ISCJ |
| 窄带瞄频噪声 | NBJ |
| 距离欺骗干扰 | RDJ |

### C++ 入口点

`signal_processing_cpp.pyd` 提供三个入口函数：
- `run_recognition()` — 干扰识别
- `run_processing_rd()` — RD 处理 (Case 1~5)
- `run_processing_decouple()` — 解耦处理 (Case 6)

## 可视化图表 (7种)

| 标签页 | 说明 |
|--------|------|
| 脉冲特性分析 | 单脉冲时域/包络特性 |
| 频域频谱 | FFT 幅度谱 (dB) |
| 信号矩阵 | 热力图 (实部/虚部/幅度/相位) |
| 距离-多普勒 | 2D RD 图 (物理坐标 m/m/s) |
| 解耦结果 | Case 6 目标/干扰分离结果 |
| 结果摘要 | 各 Case 处理结果汇总 |
| STFT 时频 | passband 载波去除 STFT |

## 配置

04_GUI_signal_processing 使用 `system.*`、`processing.*`、`recognition.*` 配置节。
同时链接模块01的 `libwaveform_core.a`，通过 temp JSON 文件注入配置参数。

配置搜索顺序：环境变量 `SPS_CONFIG` → 向上搜索 config.json（最多10层）→ `~/.config/sps/config.json`。
也支持通过命令行参数指定配置路径：`python app.py /path/to/config.json`。

---

# 构建指南

## Windows 构建

### 前置条件

1. **Python 3.13+**
2. **MinGW-w64 (GCC/G++)** — 推荐 UCRT runtime 版本
3. **CMake**
4. **C++ 源码** — 已从仓库移除，`lib/` 目录中已包含预编译的 `.pyd` 文件，可直接运行 GUI。如需重新编译静态库，需先解压对应的加密 zip 备份（密码见 `docs/BUILD_GUIDE.md`）恢复 C++ 源码。注意：模块04同时需要模块01的源码（`libwaveform_core.a`）。

### 一键构建

```cmd
cd 04_GUI_signal_processing
venv\Scripts\activate
pyinstaller --clean --noconfirm scripts\雷达信号处理系统_win.spec
```

输出在 `dist\雷达信号处理系统.exe`（单文件模式，~90MB）。

### 部署

将 `dist\雷达信号处理系统.exe` 单文件拷贝到目标机器即可运行。
- 将 `config.json` 放在 exe 上两级目录
- 或通过菜单 **文件 → 加载配置...** 手动导入
- 或设置环境变量 `SPS_CONFIG` 指向 config.json

## 平台差异

| 项目 | macOS | Windows |
|------|-------|---------|
| C++ 绑定产物 | `signal_processing_cpp.cpython-314-darwin.so` | `signal_processing_cpp.pyd` |
| 打包 spec | `雷达信号处理系统.spec` | `雷达信号处理系统_win.spec` |
| 打包输出 | `dist/雷达信号处理系统.app` | `dist/雷达信号处理系统.exe` (单文件, ~90MB) |
| 运行时 DLL | 不需要 | 需要 MinGW UCRT64 (libgcc/libstdc++/libwinpthread) |
| 任务栏图标 | 原生支持 | 需 Win32 API (SetClassLongPtrW + QTimer.singleShot) |
