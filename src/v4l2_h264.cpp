#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include "video/rkmpp_encoder.h"
#include "io/nats_client.h"
#include "utils/rk_helper.cpp"
using namespace rk_helper;

#define WIDTH 1280
#define HEIGHT 720
#define CAMERA_ID 0
#define NATS_URL "nats://192.168.3.168"

int main() {
    // 1. 打开摄像头
    cv::VideoCapture cap(CAMERA_ID, cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FRAME_WIDTH, WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, HEIGHT);
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FPS, 30);
    if (!cap.isOpened()) {
        printf("无法打开摄像头\n");
        return -1;
    }

    // 2. 创建编码器
    RKMPPEncoder encoder(WIDTH, HEIGHT, 4, AV_PIX_FMT_BGR24);
    if (!encoder.init()) {
        printf("编码器初始化失败\n");
        return -1;
    }

    // 3. 初始化NATS
    auto nats = std::make_unique<fc_io::nats_io>(NATS_URL, "cigar", "ai.streaming");
    if (!nats->io_init()) {
        printf("NATS初始化失败\n");
        return -1;
    }

    // 4. 设置编码完成回调，直接发布到nats
    encoder.set_on_encoder_ok_cb([&](uint8_t *data, int size){
        nats->write_subj("ai.streaming", reinterpret_cast<const char*>(data), size);
    });

    // 5. 启动编码线程
    std::atomic<bool> running{true};
    std::thread enc_thread([&](){ encoder.encoder(); });

    FPSCalculator fps;
    // 帧率输出线程
    std::thread fps_thread([&](){
        while(running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            printf("[FPS] 实时采集帧率: %.2f\n", fps.getFramePerSecond());
        }
    });

    // 6. 采集并送入编码
    cv::Mat frame;
    while (running) {
        if (!cap.read(frame)) {
            printf("采集失败\n");
            break;
        }
        fps.CountAFrame();
        encoder.add_data(frame.clone());
        if (cv::waitKey(1) == 27) break; // 按ESC退出
    }

    // 7. 结束
    running = false;
    encoder.release();
    enc_thread.join();
    fps_thread.join();
    cap.release();
    return 0;
} 