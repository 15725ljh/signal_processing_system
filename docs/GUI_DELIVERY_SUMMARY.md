# 雷达波形生成系统 GUI - 交付说明

## 版本信息

- **版本**: v1.0
- **交付日期**: 2026-04-03
- **适用模块**: 仅模块01 (波形生成, Case1~5)

## 交付内容

### 可执行文件

`dist/雷达波形生成系统.app` — macOS 独立应用，双击运行，无需安装Python。

### 配套文件

- `dist/config.json` — 参数配置文件（需与.app同级目录）

### 源代码

```
GUI/
├── app.py                    # 入口
├── requirements.txt          # Python 依赖
├── scripts/                  # 构建/打包脚本
│   ├── build.sh
│   ├── setup_cython.py
│   └── 雷达波形生成系统.spec
├── ui/                       # 界面源码(main_window.py 含 C++ 调用逻辑)
└── core/                     # 后端模块(Cython 编译后只有 .so)

01_waveform_generation/
├── bindings/
│   └── waveform_bind.cpp     # pybind11 绑定代码
├── src/waveform_core.cpp     # 共享波形生成核心代码
└── build/libwaveform_core.a  # 静态库(GUI 链接)
```

## 已实现功能

### 参数配置
- [x] 系统参数面板 (fc, Tp, B, prf, Vr, Rs 等)
- [x] 5种波形模式独立参数面板 (Case1~5)
- [x] 派生参数自动计算显示 (fs, gama, nrn, lambda 等)
- [x] 科学计数法输入 (如 16e9)
- [x] 参数验证与错误提示
- [x] config.json 同步读写

### 波形生成
- [x] pybind11 直接调用 C++ 核心算法 (通过 libwaveform_core.a)
- [x] 支持5种波形模式 (Case1~5)
- [x] 返回 numpy 复数矩阵 + 辅助序列 (f, phi1, freq_seq)

### 可视化
- [x] 时域波形图 (实部/虚部/包络)
- [x] 频域频谱图 (dB)
- [x] 信号矩阵热力图 (实部/虚部/幅度/相位)
- [x] 频率序列柱状图 (带十字准线)
- [x] 随机相位散点图 (Case2/5)
- [x] STFT时频分析图
- [x] 图表导出功能

### 界面
- [x] 主窗口 (菜单栏/工具栏/状态栏)
- [x] 亮色/暗色主题切换 (Catppuccin Mocha)
- [x] 控制台日志面板 (实时显示C++输出)
- [x] 进度指示

## 不包含的功能

本GUI **仅封装模块01（波形生成）**，不涉及以下模块：

- 模块02: 干扰生成 (10种干扰)
- 模块03: 干扰检测抑制 (fc=35GHz独立系统)
- 模块04: 信号处理 (6种处理算法 + 干扰识别)

这些模块通过命令行运行对应的C++可执行文件：
```bash
cd 01_waveform_generation/build && ./waveform_gen
cd ../../02_jamming_generation/build && ./jamming_gen
cd ../../03_jamming_detection_suppression/build && ./jamming_det_sup
cd ../../04_signal_processing/build && ./signal_proc
```

## 技术规格

| 项目 | 规格 |
|------|------|
| GUI框架 | PySide6 |
| 绘图库 | pyqtgraph (高性能) |
| C++绑定 | pybind11 |
| 打包工具 | PyInstaller |
| C++核心 | libwaveform_core.a (模块01静态库, 仅依赖 Eigen) |
| 支持平台 | macOS (Apple Silicon / Intel) |
| Python版本 | 3.13+ |

## 已知限制

1. **仅macOS**: 当前打包仅支持macOS .app格式
2. **仅模块01**: 不支持模块02/03/04的可视化和参数配置
3. **无文件管理器**: 不支持浏览output/目录
4. **无批量处理**: 每次只能生成一种Case
