# GUI 共享组件

四个 GUI（01_GUI_waveform、02_GUI_jamming、03_GUI_detection、04_GUI_signal_processing）共享的基础设施。

## Windows 任务栏图标

四个 GUI 使用相同的三要素方案解决 Windows 任务栏不显示自定义图标的问题：

1. `SetCurrentProcessExplicitAppUserModelID` — 创建窗口前调用
2. `SetClassLongPtrW(GCLP_HICONSM/HICON)` — 类级别图标设置
3. `QTimer.singleShot(200, ...)` — showEvent 中延迟调用

实现位于各 GUI 的 `ui/main_window.py` 和 `app.py`。

## 主题管理

支持亮色/暗色双主题切换（View 菜单），暗色使用 Catppuccin Mocha 配色方案。

实现位于 `ui/theme.py`。

## 控制台面板

实时显示 C++ 后端的 stdout 输出，支持彩色日志（INFO/WARNING/ERROR/SUCCESS），最多保留 5000 行。

实现位于 `ui/console_panel.py`。

## 配置文件搜索顺序

GUI 按以下顺序搜索 `config.json`：
1. 环境变量 `SPS_CONFIG` 指定的路径
2. 向上遍历目录树（最多 10 级）
3. `~/.config/sps/config.json`

也支持通过命令行参数指定：`python app.py /path/to/config.json`。

配置文件支持 `//` 和 `#` 行注释。未配置的参数自动使用代码内置默认值。

## 配置参数分区

| 分区 | key路径前缀 | 适用模块 | 说明 |
|------|------------|---------|------|
| 系统参数 | `system.*` | 01/02/04 | 载波频率、带宽、PRF等全局参数 |
| 波形生成 | `waveform.*` | 01 | 跳频点数、抖动范围等 |
| 干扰生成 | `jamming.*` | 02 | 10种干扰模式各自的参数 |
| 信号处理 | `processing.*` | 04 | Kaiser窗、STFT、Tsallis参数 |
| 干扰识别 | `recognition.*` | 04 | STFT频点数、阈值系数等 |
| 检测抑制 | `detection_suppression.*` | 03 | 独立参数体系(fc=35GHz) |

## 依赖

**运行时：**

| 依赖 | 版本 | 用途 |
|------|------|------|
| Python | 3.13+ | 运行环境 |
| PySide6 | 6.6+ | GUI 框架 |
| pyqtgraph | 0.13+ | 科学可视化 |
| numpy | 1.24+ | 数值计算 |
| scipy | 1.11+ | 信号处理 (STFT 等) |

**构建时（requirements-dev.txt）：**

| 依赖 | 版本 | 用途 |
|------|------|------|
| pybind11 | 2.11+ | C++ 绑定编译 |
| Cython | 3.0+ | 备用编译方案 |
| PyInstaller | 6.0+ | 单文件 exe 打包 |

## 四个 GUI 对比

| 特性 | 01_GUI_waveform | 02_GUI_jamming | 03_GUI_detection | 04_GUI_signal_processing |
|------|-------------|-------------|---------------|---------------------|
| 封装模块 | 模块01 | 模块02 | 模块03 | 模块04+01 |
| 模式数 | 5 (Case1~5) | 10 (Case1~10) | 5 (干扰类型) | 6 (Case1~6) |
| C++ 绑定 | waveform_cpp.pyd | jamming_cpp.pyd | detection_cpp.pyd | signal_processing_cpp.pyd |
| 静态库 | libwaveform_core.a | libjamming_core.a | libdetection_core.a | libsignal_processing_core.a + libwaveform_core.a |
| 参数体系 | system.* | system.* + jamming.* | detection_suppression.* | system.* + processing.* + waveform.* + detection_suppression.* |
| 载波频率 | 16 GHz | 16 GHz | 35 GHz | 16 GHz |
| 图表数 | 8种 | 7种 | 8种 | 7种 |
| C++ 入口数 | 1 | 1 | 1 | 3 (rd/decouple/recognition) |

**共享组件：** theme.py, console_panel.py, scientific_spinbox.py, signal_utils.py, 资源文件 (assets/)。

## 部署

四个 GUI 的单文件 exe 可直接拷贝到目标 Windows 机器运行。`config.json` 需放在 exe 上两级目录，或通过菜单 **文件 → 加载配置...** 手动导入，或设置环境变量 `SPS_CONFIG` 指向 config.json。
