# 跨平台编译运行手册

## 前置要求

| 工具 | 版本要求 | 说明 |
|------|----------|------|
| CMake | >= 3.14 | 构建系统 |
| C++ 编译器 | 支持 C++17 | GCC 7+ / AppleClang 12+ / MSVC 2019+ |
| Git | 任意 | 仅克隆依赖源码时需要 |

## 目录结构概览

```
signal_processing_system/
├── CMakeLists.txt                  # 顶层构建入口(add_subdirectory ×4)
├── config.json                     # 外部参数配置文件(所有模块共享)
├── cmake/FindLibraries.cmake       # 库自动检测(Eigen/FFTW/nlohmann, 优先本地 > 系统安装)
├── common/Config.h                 # 配置管理器(单例，JSON解析，智能寻址)
├── docs/                           # 统一文档目录
├── third_party/                    # 本地第三方库
│   ├── nlohmann/json.hpp           # 自实现迷你JSON解析器(~215行, 已包含)
│   ├── eigen/                      # Eigen 3.4.0 (header-only, 已包含)
│   └── fftw-install/               # FFTW 3.3.10 (需编译)
├── output/                         # 运行输出目录(运行程序后生成)
├── 01_waveform_generation/         # 模块1: 波形生成(5种模式) + 静态库
├── 02_jamming_generation/          # 模块2: 干扰生成(10种模式)
├── 03_jamming_detection_suppression/ # 模块3: 干扰识别与抑制(5种类型)
├── 04_signal_processing/           # 模块4: 信号处理(6种模式)
└── GUI_waveform/                   # PySide6 GUI (波形生成)
└── GUI_jamming/                    # PySide6 GUI (干扰生成)
```

---

## 第一步：准备第三方库

### 自实现组件（已包含，无需额外操作）

| 组件 | 文件 | 替代对象 | 说明 |
|------|------|----------|------|
| mini JSON 解析器 | `third_party/nlohmann/json.hpp` (~215行) | nlohmann/json (24765行) | 仅实现本项目 Config.h 所需的 API 子集 |
| Bessel 函数 I₀ | 各模块 `Module0.h` 中的 `bessel_i0()` | Boost `cyl_bessel_i` | Taylor 级数展开，精度到机器 epsilon |

### Eigen 3.4.0 (header-only)

Eigen 是纯头文件库，已包含在 `third_party/eigen/` 中，**无需编译**，所有平台直接可用。

### FFTW 3.3.10

FFTW 是平台相关的，必须**在目标平台上重新编译**。

#### macOS (AppleClang)

```bash
cd signal_processing_system/third_party

# 下载源码(如已有源码可跳过)
curl -O https://www.fftw.org/fftw-3.3.10.tar.gz
tar xzf fftw-3.3.10.tar.gz

# 编译安装到本地 fftw-install/
rm -rf fftw-install
mkdir -p fftw-install
cd fftw-3.3.10
./configure --prefix=$PWD/../fftw-install --disable-shared --enable-static --with-sse2 --enable-avx
make -j$(sysctl -n hw.ncpu)
make install
cd ..
```

#### Linux (GCC)

```bash
cd signal_processing_system/third_party

curl -O https://www.fftw.org/fftw-3.3.10.tar.gz
tar xzf fftw-3.3.10.tar.gz

rm -rf fftw-install
mkdir -p fftw-install
cd fftw-3.3.10
./configure --prefix=$PWD/../fftw-install --disable-shared --enable-static --with-sse2 --enable-avx --enable-openmp
make -j$(nproc)
make install
cd ..
```

> Linux 下建议开启 `--enable-openmp`，03模块支持OpenMP并行加速。

#### Windows (MSVC)

```bash
# 方式1: 使用vcpkg
vcpkg install fftw3:x64-windows-static

# 方式2: 手动编译(在VS Developer Command Prompt中)
cd third_party\fftw-3.3.10
nmake /f Makefile.win32 lib
```

### 第三方库文件清单(编译后应存在的文件)

```
third_party/
├── nlohmann/json.hpp            # 自实现迷你JSON解析器 (已包含)
├── eigen/Eigen/                 # Eigen头文件 (已包含)
└── fftw-install/
    ├── include/fftw3.h          # FFTW头文件
    └── lib/libfftw3.a           # FFTW静态库 (macOS/Linux)
    └── lib/fftw3.lib            # FFTW静态库 (Windows)
```

---

## 第二步：编译项目

### 方式一：各模块独立构建（推荐）

每个模块在自己的 `build/` 目录中独立构建：

```bash
cd signal_processing_system

# 模块1: 波形生成（同时构建 libwaveform_core.a 静态库）
cd 01_waveform_generation
mkdir -p build && cd build
cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../..

# 模块2: 干扰生成
cd 02_jamming_generation
mkdir -p build && cd build
cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../..

# 模块3: 干扰识别与抑制
cd 03_jamming_detection_suppression
mkdir -p build && cd build
cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../..

# 模块4: 信号处理
cd 04_signal_processing
mkdir -p build && cd build
cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../..
```

### 方式二：统一构建（从项目根目录）

```bash
cd signal_processing_system
mkdir -p build && cd build
cmake ..
cmake --build . -j$(sysctl -n hw.ncpu)      # macOS
# cmake --build . -j$(nproc)                # Linux
# cmake --build . --config Release          # Windows
```

编译成功后，生成4个可执行文件：

| 可执行文件 | 对应模块 |
|-----------|----------|
| `01_waveform_generation/waveform_gen` | 波形生成 |
| `02_jamming_generation/jamming_gen` | 干扰生成 |
| `03_jamming_detection_suppression/jamming_det_sup` | 干扰识别与抑制 |
| `04_signal_processing/signal_proc` | 信号处理 |

模块01额外生成 `libwaveform_core.a` 静态库，供 GUI 的 pybind11 绑定使用。

### 单独编译某个模块

```bash
# 独立构建模式
cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# 统一构建模式下指定目标
cd signal_processing_system/build
cmake --build . --target waveform_gen
cmake --build . --target jamming_det_sup
```

### 库检测优先级

`cmake/FindLibraries.cmake` 按以下顺序查找库：

1. `third_party/xxx-install/` (本地编译，**推荐**)
2. `/opt/homebrew/` (macOS Homebrew)
3. `/usr/local/` (Linux 系统安装)
4. `/usr/` (Linux 系统安装)

CMake 配置时会打印检测到的库路径，确认输出中 `Eigen`、`FFTW` 路径正确。

---

## 第三步：配置参数(可选)

项目根目录的 `config.json` 包含所有可配置参数及详细注释。支持以下特性：

- **行注释**: `//` 或 `#` 开头的行会被自动忽略
- **默认值**: 删除或注释掉某行即恢复该参数的代码内置默认值
- **null值**: 设为 `null` 等同于未设置，使用默认值
- **热生效**: 修改后保存文件，重新运行程序即可，无需重新编译

### 智能寻址顺序

1. 环境变量 `SPS_CONFIG` 指定的路径
2. 可执行文件同目录: `./config.json`
3. 项目根目录: `../config.json`
4. 用户配置目录: `~/.config/sps/config.json`
5. 以上均未找到 → 使用全部默认值，控制台输出提示

### 指定自定义配置文件

```bash
# 方式1: 环境变量
export SPS_CONFIG=/path/to/my_config.json
./build/01_waveform_generation/waveform_gen

# 方式2: 复制到默认搜索路径
cp my_config.json ./build/config.json
cd build && ./01_waveform_generation/waveform_gen
```

---

## 第四步：运行程序

所有模块自动遍历全部模式，无需人工输入。运行后输出文件统一存放到 `output/` 目录。

### macOS / Linux

```bash
cd signal_processing_system

# 运行全部模块(按顺序)
./build/01_waveform_generation/waveform_gen
./build/02_jamming_generation/jamming_gen
./build/03_jamming_detection_suppression/jamming_det_sup
./build/04_signal_processing/signal_proc
```

### Windows

```bash
cd signal_processing_system
build\01_waveform_generation\Release\waveform_gen.exe
build\02_jamming_generation\Release\jamming_gen.exe
build\03_jamming_detection_suppression\Release\jamming_det_sup.exe
build\04_signal_processing\Release\signal_proc.exe
```

### 各模块运行说明

| 模块 | 运行时间(参考) | 遍历模式 | 输出文件 |
|------|---------------|----------|----------|
| 01 | ~1秒 | 5种波形 | `01_waveform_Case{1-5}_*.dat` |
| 02 | ~2秒 | 10种干扰 | `02_jamming_Case{1-10}_*.dat` |
| 03 | ~30秒 | 5类干扰×100CPI | `03_detection_*.txt`, `03_detection_*.log` |
| 04 | ~5秒 | 6种处理 | `04_processing_Case{1-6}_*.dat` |

---

## 第五步：查看结果

### 03模块测试汇总

```bash
cat output/03_detection_识别与抑制日志_log.txt
```

输出示例：
```
==================== 干扰识别与抑制测试 ====================
参数: B=80MHz, Tp=12us, CpiNum=100

RealLabel=1 | 识别=1 (ISDJ/ISRJ/ISCJ) | 正确=100/100 | ISR=18.52 dB | 耗时=0.312s
...
==================== 汇总 ====================
总正确: 500/500 | 总耗时: 1.523s
==================================================
```

### 输出文件格式

- `.dat` 文件首行为 `行数 列数`，后续每行两个数分别表示复数的实部和虚部
- `.txt` 文件每行8列：`回波实部 回波虚部 干扰实部 干扰虚部 分离干扰实部 分离干扰虚部 分离目标实部 分离目标虚部`

---

## 常见问题

### Q: CMake报错找不到Eigen

确保 `third_party/eigen/Eigen/Dense` 文件存在。如使用系统安装的Eigen，检查路径：
```bash
ls /opt/homebrew/include/eigen3/Eigen/Dense   # macOS
ls /usr/include/eigen3/Eigen/Dense             # Linux
```

### Q: CMake报错找不到FFTW

确保 `third_party/fftw-install/lib/libfftw3.a` 存在。检查编译时是否使用了正确的 `--prefix` 路径。

### Q: macOS下OpenMP不可用

AppleClang不支持OpenMP，03模块会自动回退到单线程模式，功能完全正常。Linux下GCC默认支持OpenMP。

### Q: 04模块Case6输出全零或nan

当输入信号 `Radar_Sig` 全零时(默认初始化)，`JamTarDivi` 产生nan输出，属于正常现象，不影响功能。

### Q: [Config] 未找到外部配置文件

这是正常提示。程序会使用代码内置默认值运行。如需使用自定义参数，将 `config.json` 放到项目根目录或设置 `SPS_CONFIG` 环境变量。

### Q: IDE的LSP报大量错误(Eigen/Config找不到等)

这是因为IDE没有cmake的include路径，不影响实际编译。`cmake --build` 可以正常编译通过。

### Q: 重新编译

```bash
# 独立构建模式
cd 01_waveform_generation && rm -rf build && mkdir build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# 统一构建模式
cd signal_processing_system && rm -rf build && mkdir build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
```

---

## 完整一键脚本 (macOS/Linux)

```bash
#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_DIR"

echo "=== 1. 编译FFTW ==="
cd third_party
if [ ! -f fftw-3.3.10/configure ]; then
    curl -O https://www.fftw.org/fftw-3.3.10.tar.gz
    tar xzf fftw-3.3.10.tar.gz
fi
cd fftw-3.3.10
./configure --prefix=$PWD/../fftw-install --disable-shared --enable-static
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
make install

echo "=== 2. 编译项目 ==="
cd "$PROJECT_DIR"

# 各模块独立构建
for mod in 01_waveform_generation 02_jamming_generation 03_jamming_detection_suppression 04_signal_processing; do
    echo "--- 构建 $mod ---"
    cd "$PROJECT_DIR/$mod"
    rm -rf build
    mkdir build && cd build
    cmake .. && cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
done

echo "=== 3. 运行测试 ==="
cd "$PROJECT_DIR"
for mod in 01_waveform_generation 02_jamming_generation 03_jamming_detection_suppression 04_signal_processing; do
    echo "--- 运行 $mod ---"
    ./$mod/build/$(basename $mod | sed 's/^[0-9]*_//' | sed 's/_.*//')_gen 2>/dev/null || \
    ./$mod/build/$(cat $mod/CMakeLists.txt | grep -m1 'add_executable' | sed 's/.*(\([^ ]*\).*/\1/')
done

echo "=== 完成! 输出文件在 output/ 目录 ==="
```

使用方式：保存为 `build_and_run.sh`，放到项目根目录，执行 `chmod +x build_and_run.sh && ./build_and_run.sh`。
