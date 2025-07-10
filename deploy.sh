#!/bin/bash

# RK3588 智能视频流推理与推流系统 - 快速部署脚本
# 使用方法: ./deploy.sh [nats|ai|all|status|stop]

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

# 检查是否为root用户
check_root() {
    if [ "$EUID" -ne 0 ]; then
        log_error "请使用root权限运行此脚本"
        exit 1
    fi
}

# 下载NATS服务器
download_nats_server() {
    log_info "下载NATS服务器..."
    
    local nats_version="v2.10.20"
    local nats_url="https://gitdl.cn/https://github.com/nats-io/nats-server/releases/download/${nats_version}/nats-server-${nats_version}-linux-arm64.tar.gz"
    local temp_dir="/tmp/nats_download"
    
    mkdir -p $temp_dir
    cd $temp_dir
    
    if [ ! -f "nats-server-${nats_version}-linux-arm64.tar.gz" ]; then
        wget $nats_url
    fi
    
    tar -xzf "nats-server-${nats_version}-linux-arm64.tar.gz"
    sudo mv nats-server-${nats_version}-linux-arm64/nats-server /usr/local/bin/
    sudo chmod +x /usr/local/bin/nats-server
    
    cd - > /dev/null
    rm -rf $temp_dir
    
    log_success "NATS服务器下载完成"
}

# 配置NATS服务器
setup_nats_server() {
    log_info "配置NATS服务器..."
    
    # 创建配置目录
    sudo mkdir -p /home/orangepi/app
    
    # 创建NATS配置文件
    sudo tee /home/orangepi/app/nats.conf > /dev/null <<EOF
server_name: "rk3588_nats"

jetstream {
  enabled: true
  store_dir: "/home/orangepi/app/jetstream"
}

mqtt {
  port: 21888
}

http_port: 8222

websocket {
    port: 21443
    no_tls: true
}

{
max_payload: 8MB
max_pending: 16MB
}
EOF

    # 创建JetStream存储目录
    sudo mkdir -p /home/orangepi/app/jetstream
    sudo chown -R orangepi:orangepi /home/orangepi/app 2>/dev/null || true
    
    # 创建NATS服务文件
    sudo tee /etc/systemd/system/nats-server.service > /dev/null <<EOF
[Unit]
Description=NATS server for RK3588 AI system
After=network.target
Wants=network.target

[Service]
Type=simple
User=root
ExecStart=/usr/local/bin/nats-server -c /home/orangepi/app/nats.conf
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

    # 重新加载systemd
    sudo systemctl daemon-reload
    
    # 启用并启动NATS服务
    sudo systemctl enable nats-server
    sudo systemctl start nats-server
    
    log_success "NATS服务器配置完成"
}

# 配置AI服务
setup_ai_service() {
    log_info "配置AI服务..."
    
    # 检查可执行文件是否存在
    if [ ! -f "build/Ai" ]; then
        log_error "AI可执行文件不存在，请先编译项目"
        exit 1
    fi
    
    # 创建运行目录
    sudo mkdir -p /root/ai/run
    
    # 复制文件
    sudo cp build/Ai /root/ai/run/
    sudo cp build/v4l2_h264 /root/ai/run/ 2>/dev/null || log_warning "v4l2_h264不存在"
    
    # 复制模型文件
    if [ -f "models/yolov8-4.rknn" ]; then
        sudo cp models/yolov8-4.rknn /root/ai/run/
    else
        log_warning "模型文件不存在，请确保models/yolov8-4.rknn存在"
    fi
    
    # 复制其他文件
    sudo cp run/car.bin /root/ai/run/ 2>/dev/null || log_warning "car.bin不存在"
    
    # 设置权限
    sudo chmod +x /root/ai/run/Ai
    sudo chmod +x /root/ai/run/v4l2_h264 2>/dev/null || true
    
    # 创建AI服务文件
    sudo tee /etc/systemd/system/ai.service > /dev/null <<EOF
[Unit]
Description=RK3588 AI Video Streaming Service
After=network.target nats-server.service
Wants=nats-server.service

[Service]
Type=simple
User=root
WorkingDirectory=/root/ai/run
ExecStart=/root/ai/run/Ai
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

    # 重新加载systemd
    sudo systemctl daemon-reload
    
    # 启用AI服务
    sudo systemctl enable ai.service
    
    log_success "AI服务配置完成"
}

# 启动服务
start_services() {
    log_info "启动服务..."
    
    # 启动NATS服务器
    if systemctl is-active --quiet nats-server; then
        log_info "NATS服务器已在运行"
    else
        sudo systemctl start nats-server
        log_success "NATS服务器已启动"
    fi
    
    # 等待NATS服务器完全启动
    sleep 2
    
    # 启动AI服务
    if systemctl is-active --quiet ai.service; then
        log_info "AI服务已在运行"
    else
        sudo systemctl start ai.service
        log_success "AI服务已启动"
    fi
}

# 停止服务
stop_services() {
    log_info "停止服务..."
    
    sudo systemctl stop ai.service 2>/dev/null || true
    sudo systemctl stop nats-server 2>/dev/null || true
    
    log_success "服务已停止"
}

# 查看服务状态
show_status() {
    echo "=== 服务状态 ==="
    echo ""
    
    echo "NATS服务器状态:"
    systemctl status nats-server --no-pager -l || echo "NATS服务器未运行"
    echo ""
    
    echo "AI服务状态:"
    systemctl status ai.service --no-pager -l || echo "AI服务未运行"
    echo ""
    
    echo "=== 端口监听状态 ==="
    netstat -tlnp | grep -E "(4222|8222|21888|21443)" || echo "未发现相关端口监听"
    echo ""
    
    echo "=== 进程状态 ==="
    ps aux | grep -E "(nats-server|Ai)" | grep -v grep || echo "未发现相关进程"
}

# 检查摄像头
check_camera() {
    log_info "检查摄像头..."
    
    if [ -e "/dev/video0" ]; then
        log_success "发现摄像头设备: /dev/video0"
        
        # 检查摄像头权限
        if [ -r "/dev/video0" ] && [ -w "/dev/video0" ]; then
            log_success "摄像头权限正常"
        else
            log_warning "摄像头权限不足，尝试修复..."
            sudo chmod 666 /dev/video0
        fi
        
        # 显示摄像头信息
        echo "摄像头支持格式:"
        v4l2-ctl --list-formats-ext -d /dev/video0 2>/dev/null || echo "无法获取摄像头信息"
    else
        log_warning "未发现摄像头设备，请检查USB摄像头连接"
    fi
}

# 显示帮助信息
show_help() {
    echo "使用方法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  nats     仅配置和启动NATS服务器"
    echo "  ai       仅配置和启动AI服务"
    echo "  all      配置和启动所有服务"
    echo "  status   查看服务状态"
    echo "  stop     停止所有服务"
    echo "  camera   检查摄像头状态"
    echo "  help     显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 all      # 完整部署"
    echo "  $0 status   # 查看状态"
    echo "  $0 stop     # 停止服务"
}

# 主函数
main() {
    case "${1:-}" in
        "nats")
            check_root
            download_nats_server
            setup_nats_server
            start_services
            ;;
        "ai")
            check_root
            setup_ai_service
            start_services
            ;;
        "all")
            check_root
            download_nats_server
            setup_nats_server
            setup_ai_service
            start_services
            check_camera
            ;;
        "status")
            show_status
            ;;
        "stop")
            check_root
            stop_services
            ;;
        "camera")
            check_camera
            ;;
        "help"|"-h"|"--help")
            show_help
            ;;
        "")
            log_error "请指定操作选项"
            show_help
            exit 1
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