#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GUI_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$GUI_ROOT"

PY="./venv/bin/python3"
THIRD_PARTY="../../third_party"

usage() {
    echo "用法: bash scripts/build.sh [命令]"
    echo ""
    echo "  setup    首次安装依赖（创建 venv + 安装包）"
    echo "  cpp      编译 C++ 绑定模块"
    echo "  cython   编译 core/ 为 .so"
    echo "  app      打包 .app"
    echo "  all      执行 cpp + cython + app"
    echo "  run      运行 GUI"
    echo ""
    echo "无参数时等同于 run"
}

cmd_setup() {
    if [ ! -d "venv" ]; then
        python3 -m venv venv
    fi
    $PY -m pip install --no-cache-dir --upgrade pip -q
    $PY -m pip install --no-cache-dir -r requirements.txt -q
    $PY -m pip install --no-cache-dir cython pybind11 -q
    echo "[完成] 依赖已安装"
}

cmd_cpp() {
    echo "[1/3] 编译 C++ 绑定..."
    PYBIND11_INC=$($PY -c "import pybind11; print(pybind11.get_include())")
    PYTHON_INC=$($PY -c "import sysconfig; print(sysconfig.get_path('include'))")
    PROJECT_ROOT="$(cd "$GUI_ROOT/.." && pwd)"
    MODULE01_INC="$PROJECT_ROOT/01_waveform_generation/include"
    MODULE01_LIB="$PROJECT_ROOT/01_waveform_generation/build/libwaveform_core.a"
    BINDING_SRC="$PROJECT_ROOT/01_waveform_generation/bindings/waveform_bind.cpp"

    if [ ! -f "$MODULE01_LIB" ]; then
        echo "[错误] 静态库未构建,请先运行: cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . --target waveform_core"
        exit 1
    fi

    clang++ -O3 -std=c++17 -shared -undefined dynamic_lookup \
        -I"$PYBIND11_INC" \
        -I"$PYTHON_INC" \
        -I"$THIRD_PARTY/eigen" \
        -I"$MODULE01_INC" \
        "$BINDING_SRC" \
        "$MODULE01_LIB" \
        -o waveform_cpp.cpython-314-darwin.so
    echo "[完成] waveform_cpp.so (linked with 01_waveform_generation/libwaveform_core.a)"
}

cmd_cython() {
    echo "[2/3] 编译 core/ 模块..."
    if [ ! -f "core/config_manager.py" ]; then
        echo "[跳过] core/ 已是 .so（需要 .py 源码才能重编译）"
        return 0
    fi
    $PY "$SCRIPT_DIR/setup_cython.py" build_ext --inplace 2>&1 | grep -E "(copying|error)"
    rm -f core/config_manager.py core/signal_utils.py core/*.c
    rm -rf core/__pycache__
    echo "[完成] core/*.so"
}

cmd_app() {
    echo "[3/3] 打包 .app..."
    rm -rf build/ dist/
    $PY -m PyInstaller --clean "$SCRIPT_DIR/雷达波形生成系统.spec" 2>&1 | tail -3
    rm -rf "dist/雷达波形生成系统.app/Contents/Resources/core/__pycache__"
    echo "[完成] dist/雷达波形生成系统.app"
}

cmd_run() {
    $PY app.py "$@"
}

case "${1:-run}" in
    setup)  cmd_setup ;;
    cpp)    cmd_cpp ;;
    cython) cmd_cython ;;
    app)    cmd_app ;;
    all)    cmd_cpp && cmd_cython && cmd_app ;;
    run)    cmd_run ;;
    *)      usage ;;
esac
