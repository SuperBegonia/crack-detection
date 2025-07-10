#!/bin/bash

# RK3588 智能视频流推理与推流系统 - 自动化编译脚本
# 使用方法: ./build.sh [clean|rebuild|install]

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查系统架构
check_architecture() {
    local arch=$(uname -m)
    if [ "$arch" != "aarch64" ]; then
        log_warning "当前系统架构为 $arch，推荐在 aarch64 架构下编译"
        read -p "是否继续编译？(y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

# 检查依赖
check_dependencies() {
    log_info "检查系统依赖..."
    
    local deps=("cmake" "make" "g++" "git" "pkg-config")
    local missing_deps=()
    
    for dep in "${deps[@]}"; do
        if ! command -v $dep &> /dev/null; then
            missing_deps+=($dep)
        fi
    done
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "缺少以下依赖: ${missing_deps[*]}"
        log_info "请运行: sudo apt update && sudo apt install -y build-essential cmake git pkg-config"
        exit 1
    fi
    
    log_success "系统依赖检查完成"
}

# 检查第三方库
check_thirdparty_libs() {
    log_info "检查第三方库..."
    
    local missing_libs=()
    
    # 检查ffmpeg-rockchip
    if [ ! -d "3rdparty/ffmpeg-rockchip/lib" ]; then
        missing_libs+=("ffmpeg-rockchip")
    fi
    
    # 检查RGA库
    if [ ! -f "3rdparty/rga/RK3588/lib/Linux/aarch64/librga.a" ]; then
        missing_libs+=("RGA库")
    fi
    
    # 检查NATS库
    if [ ! -f "3rdparty/nats.c/build/lib/libnats_static.a" ]; then
        missing_libs+=("NATS库")
    fi
    
    # 检查RKNN库
    if [ ! -f "librknn_api/aarch64/librknnrt.so" ]; then
        missing_libs+=("RKNN库")
    fi
    
    if [ ${#missing_libs[@]} -ne 0 ]; then
        log_warning "缺少以下第三方库: ${missing_libs[*]}"
        log_info "请参考 Linux编译烧录指南.md 中的步骤2进行编译"
        read -p "是否继续编译？(y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
    
    log_success "第三方库检查完成"
}

# 编译NATS库
compile_nats() {
    if [ ! -f "3rdparty/nats.c/build/lib/libnats_static.a" ]; then
        log_info "编译NATS C客户端..."
        cd 3rdparty/nats.c
        mkdir -p build
        cd build
        cmake .. -DNATS_BUILD_STREAMING=OFF
        make -j$(nproc)
        cd ../../..
        log_success "NATS库编译完成"
    else
        log_info "NATS库已存在，跳过编译"
    fi
}

# 清理构建目录
clean_build() {
    log_info "清理构建目录..."
    rm -rf build
    log_success "清理完成"
}

# 编译项目
compile_project() {
    log_info "开始编译项目..."
    
    # 创建构建目录
    mkdir -p build
    cd build
    
    # 配置CMake
    log_info "配置CMake..."
    cmake ..
    
    # 编译
    log_info "编译项目..."
    make -j$(nproc)
    
    cd ..
    log_success "项目编译完成"
}

# 安装到系统
install_to_system() {
    log_info "安装到系统..."
    
    # 创建运行目录
    sudo mkdir -p /root/ai/run
    
    # 复制可执行文件
    sudo cp build/Ai /root/ai/run/ 2>/dev/null || log_warning "Ai可执行文件不存在"
    sudo cp build/v4l2_h264 /root/ai/run/ 2>/dev/null || log_warning "v4l2_h264可执行文件不存在"
    
    # 复制模型文件
    if [ -f "models/yolov8-4.rknn" ]; then
        sudo cp models/yolov8-4.rknn /root/ai/run/
    else
        log_warning "模型文件 models/yolov8-4.rknn 不存在"
    fi
    
    # 复制其他必要文件
    sudo cp run/car.bin /root/ai/run/ 2>/dev/null || log_warning "car.bin文件不存在"
    
    # 设置权限
    sudo chmod +x /root/ai/run/Ai 2>/dev/null || true
    sudo chmod +x /root/ai/run/v4l2_h264 2>/dev/null || true
    
    log_success "安装完成"
}

# 显示帮助信息
show_help() {
    echo "使用方法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  clean    清理构建目录"
    echo "  rebuild  重新编译项目"
    echo "  install  安装到系统"
    echo "  help     显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0          # 编译项目"
    echo "  $0 clean    # 清理构建目录"
    echo "  $0 rebuild  # 重新编译"
    echo "  $0 install  # 安装到系统"
}

# 主函数
main() {
    case "${1:-}" in
        "clean")
            clean_build
            ;;
        "rebuild")
            clean_build
            check_architecture
            check_dependencies
            check_thirdparty_libs
            compile_nats
            compile_project
            ;;
        "install")
            install_to_system
            ;;
        "help"|"-h"|"--help")
            show_help
            ;;
        "")
            check_architecture
            check_dependencies
            check_thirdparty_libs
            compile_nats
            compile_project
            ;;
        *)
            log_error "未知选项: $1"
            show_help
            exit 1
            ;;
    esac
}

# 执行主函数
main "$@" 