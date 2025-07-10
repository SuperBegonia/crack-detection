#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>

#include <condition_variable>
#include <opencv2/opencv.hpp>

// 项目特定的头文件

#include "io/nats_client.h"
#include "video/rkmpp_decoder.h"
#include "yolo/Yolov8Detection.h"
#include "utils/logging.h"
#include "draw/cv_draw.h"
#include "yolo/yolov8_thread_pool.h"
#include "video/rkmpp_encoder.h"
#include "utils/rk_helper.cpp"
#include "io/CircularQueue.h"
#include "io/udp.h"
#include "msg/msg.h"
#include "types/video_infos_type.h"
#include "utils/json.hpp"
#include <bits/fs_fwd.h>
using namespace rk_helper;
#define UDP_LISTEN_PORT 8818
#define UDP_SEND_IP "192.168.20.7"
#define UDP_SEND_PORT 20000
#define NATS_URL "nats://192.168.199.33"

using json = nlohmann::json;



struct AIConfig{
    std::string SendIP;
    std::string License;
    int SendPort;
    int ListenPort;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AIConfig,SendIP, SendPort, ListenPort)
};


// 全局状态管理类
class GlobalState
{
public:
    std::unique_ptr<RKMPPEncoder> encoder;
    FPSCalculator FPS;
    FPSCalculator AIFPS;
    FPSCalculator NatsFPS;

    int frame_start_id = 0;
    int frame_end_id = 0;
    std::chrono::system_clock::time_point last_write_img = std::chrono::system_clock::now();

    std::unique_ptr<ThreadPool> thread_pool;
    bool yolo_end = false;

    std::unique_ptr<fc_io::nats_io> nats_io_instance;
    std::unique_ptr<PacketManager> packet_manager;

    std::unique_ptr<UdpSocket> udp_receiver;
    std::unique_ptr<UDPSender> udp_sender;
    bool isNeedSet = false;

    std::mutex mtx;
    std::condition_variable cv;

    std::string lp = "0";
    int offset = 0;
    AIConfig config;

} global;

// 工具函数

/**
 * @brief 序列化检测结果为字节缓冲区
 *
 * @param detections 检测结果向量
 * @return std::vector<uint8_t> 序列化后的字节缓冲区
 */
std::vector<uint8_t> serializeDetections(const std::vector<Detection> &detections)
{
    std::vector<uint8_t> buffer;
    for (const auto &d : detections)
    {
        buffer.push_back(static_cast<uint8_t>(d.class_id));
        buffer.push_back(static_cast<uint8_t>(d.confidence * 100));

        auto serialize_box = [&](uint16_t value)
        {
            buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF)); // 高字节
            buffer.push_back(static_cast<uint8_t>(value & 0xFF));        // 低字节
        };

        serialize_box(static_cast<uint16_t>(d.box.x));
        serialize_box(static_cast<uint16_t>(d.box.y));
        serialize_box(static_cast<uint16_t>(d.box.width));
        serialize_box(static_cast<uint16_t>(d.box.height));
    }
    return buffer;
}

/**
 * @brief 获取当前系统时间的字符串表示
 *
 * @return std::string 格式化后的当前时间字符串
 */
std::string get_current_time_as_string()
{
    auto now = std::chrono::system_clock::now();
    auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto now_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    auto now_local = std::localtime(&now_seconds);
    std::stringstream ss;
    ss << std::put_time(now_local, "%Y-%m-%d--%H-%M-%S");
    ss << '-' << std::setw(3) << std::setfill('0') << now_milliseconds;

    return ss.str();
}

// 初始化函数


/**
 * @brief 初始化UDP发送器
 *
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool initializeUDPSender()
{
    global.udp_sender = std::make_unique<UDPSender>(global.config.SendIP, global.config.SendPort);
    return true;
}

/**
 * @brief 初始化数据包管理器
 *
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool initializePacketManager()
{
    global.packet_manager = std::make_unique<PacketManager>(1024 * 1024, 1470);
    return true;
}

/**
 * @brief 初始化编码器
 *
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool initializeEncoder()
{
    global.encoder = std::make_unique<RKMPPEncoder>(1920, 1088, 4);
    if (!global.encoder->init())
    {
        NN_LOG_ERROR("编码器初始化失败！");
        return false;
    }
    return true;
}

/**
 * @brief 初始化解码器
 *
 * @param decoder 解码器实例
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool initializeDecoder(FCourier::RKMPPDecoder &decoder)
{
    if (decoder.init())
    {
        printf("解码器初始化成功。\n");
        decoder.start();
        return true;
    }
    else
    {
        printf("解码器初始化失败。\n");
        return false;
    }
}

/**
 * @brief 初始化UDP接收器
 *
 * @param decoder 解码器实例
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool initializeUDPReceiver(FCourier::RKMPPDecoder &decoder)
{
    global.udp_receiver = std::make_unique<UdpSocket>([&decoder](const char *data, int length)
                                                      { decoder.set_raw_data((uint8_t *)(data), length); });
    global.udp_receiver->bindSocket(UDP_LISTEN_PORT);
    global.udp_receiver->startReceiving();
    return true;
}

/**
 * @brief 初始化NATS IO
 *
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool initializeNATS()
{
    // 根据需要初始化
    global.nats_io_instance = std::make_unique<fc_io::nats_io>(NATS_URL, "cigar", "cigar");
    global.nats_io_instance->io_init();
    return true;
}

/**
 * @brief 初始化线程池
 *
 * @param model_path 模型路径
 * @param threads 线程数量
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool initializeThreadPool(std::string model_path, int threads)
{ // 传递 by value

    global.thread_pool = std::make_unique<ThreadPool>();
    global.thread_pool->need_draw = true;
    global.thread_pool->startTPool(model_path, threads);
    return true;
}

/**
 * @brief 设置解码器和编码器的回调函数
 *
 * @param decoder 解码器实例
 */
void setupCallbacks(FCourier::RKMPPDecoder &decoder)
{
    // 解码器回调
    decoder.set_callback([](unsigned char *data, int size, int w, int h, AVPixelFormat format, void *handler)
                         { global.FPS.CountAFrame(); });
    decoder.set_mat_callback([](cv::Mat mat, video_decoder_info info, void *handler)
                             {

        if(!global.isNeedSet){
            global.isNeedSet = true;
           // global.encoder->add_data(mat.clone());
            return;
        }

        if (global.thread_pool) {
            // 分配新的帧ID并添加任务到线程池
            global.thread_pool->new_id = global.frame_start_id + 1;
            global.thread_pool->addTask(mat.clone(), global.frame_start_id++);
            global.isNeedSet = false;
        } });

    // 编码器回调
    global.encoder->set_on_encoder_ok_cb([](uint8_t *data, int size)
                                         {
        global.AIFPS.CountFrames(size);
        if (global.packet_manager) {
            global.packet_manager->SplitIntoPackets(reinterpret_cast<const char *>(data), size);
        } });
}

// 线程函数

/**
 * @brief 处理YOLO模型的结果
 */
void GetYoloResults()
{

    global.offset = global.lp == "l" ? 0 : 20;

    // 不满足认证 随机黑屏
    int c = 0;
    switch (global.offset)
    {
    case 20:
        c = generateRandomNumber(1, 500);
        break;

    default:
        break;
    }

    int total = 0;
    while (true)
    {
        total++;

        cv::Mat img;
        int id = global.frame_end_id++;
        auto ret = global.thread_pool->getTargetImgResult(img, id);

        if (img.empty())
        {
            NN_LOG_ERROR("接收到空图像。");
            continue;
        }

        if (ret != NN_SUCCESS)
        {
            NN_LOG_INFO("获取目标图像结果时出错。");
            continue;
        }

        if (c != 0 && total > c * 30)
        {
            // 暗桩
            cv::Mat whiteImage = cv::Mat::ones(img.rows, img.cols, CV_8UC3) * 255;
            global.encoder->add_data(whiteImage.clone());
        }
        else
        {
            // 将图像添加到编码器
            global.encoder->add_data(img.clone());
        }

        // 准备AI信息
        std::vector<AI_MSG::Data> ai_infos;
        std::vector<Detection> objects;
        ret = global.thread_pool->getTargetResult(objects, id);

        if (ret != NN_SUCCESS)
        {
            NN_LOG_INFO("获取目标检测结果时出错。");
            continue;
        }

        for (const auto &obj : objects)
        {
            AI_MSG::Data d;
            d.x = obj.box.x;
            d.y = obj.box.y;
            d.width = obj.box.width; // 修正：从height改为width
            d.height = obj.box.height;
            d.score = obj.confidence;
            d.class_id = obj.class_id;
            ai_infos.push_back(d);
        }

        // 如果检测到过多物体且时间间隔满足条件，保存图像
        if (objects.size() > 9999)
        {
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - global.last_write_img).count();
            if (elapsed >= 5)
            {
                std::string filename = "/images/output_" + get_current_time_as_string() + ".jpg";
                std::cout << "保存图片: " << filename << std::endl;
                cv::imwrite(filename, img);
                global.last_write_img = now;
            }
        }

        // 序列化并发送AI信息
        auto serialized_data = AI_MSG::serialize(ai_infos);
        if (global.nats_io_instance)
        {
            global.nats_io_instance->write_subj("ai.infos", reinterpret_cast<char *>(serialized_data.data()), serialized_data.size());
        }

        // 检查是否需要结束
        if (global.yolo_end && ret != NN_SUCCESS)
        {
            global.thread_pool->stopAll();
            break;
        }
    }

    // 确保所有线程停止
    global.thread_pool->stopAll();
}



/**
 * @brief 启动编码器
 */
void EncoderStart()
{
    global.encoder->encoder();
}

/**
 * @brief 发布流媒体数据
 */
void PublishStreaming()
{
    PacketManager::DataPacket packet;
    while (true)
    {
        if (global.packet_manager->TryGetNextPacket(packet))
        {
            if (global.nats_io_instance && false)
            {
                global.nats_io_instance->write_subj("ai.streaming", packet.data(), packet.size());
                global.NatsFPS.CountFrames(packet.size());
            }
            if (!global.udp_sender->send_data(packet.data(), packet.size()))
            {
                printf("UDP发送失败！\n");
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }
}

/**
 * @brief 启动所有线程
 */
void startThreads()
{
    std::thread result_thread(GetYoloResults);
    std::thread encoder_thread(EncoderStart);
    std::thread publish_thread(PublishStreaming);
    // 如果需要跟踪结果，取消注释
    // std::thread tracking_thread(EncoderTrackingResult);

    // 分离线程以允许独立运行
    result_thread.detach();
    encoder_thread.detach();
    publish_thread.detach();
    // tracking_thread.detach();
}

/**
 * @brief 主监控和日志记录
 *
 * @param decoder 解码器实例
 */
void monitorAndLog(FCourier::RKMPPDecoder &decoder)
{
    while (true)
    {
        global.AIFPS.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        NN_LOG_INFO("解码器FPS: %d", decoder.get_fps());
        NN_LOG_INFO("推理及编码速率: %lf kB/s", global.AIFPS.getFramePerSecond() / 1024.0);
        NN_LOG_INFO("NATS发布速率: %lf kB/s\n", global.NatsFPS.getFramePerSecond() / 1024.0);
    }
}


/**
 * @brief 检查文件是否存在
 *
 * @param filename 文件名
 * @return true 文件存在
 * @return false 文件不存在
 */
bool fileExists(const std::string& filename) {
    std::ifstream file(filename);
    return file.good(); // 如果文件存在且可以打开，返回true
}

/**
 * @brief 读取配置文件
 */
void  LoadConfig(){

    // 判断文件是否存在
    if (!fileExists("./config.json")) {
        NN_LOG_ERROR("配置文件不存在！");
        exit(1);
    }


    // 解析配置文件
    std::ifstream file("./config.json");
    nlohmann::json config;
    try
    {
        /* code */
        file >> config;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        exit(1);
    }

    global.config = AIConfig(config);
};


// 主函数
int main()
{
    LoadConfig();
    FCourier::RKMPPDecoder decoder;

    // 初始化各模块
    if (
        !initializeUDPSender() ||
        !initializePacketManager() ||
        !initializeEncoder() ||
        !initializeDecoder(decoder) ||
        !initializeUDPReceiver(decoder) ||
        !initializeNATS() ||
        !initializeThreadPool("./car.bin", 3))
    {
        NN_LOG_ERROR("初始化过程中出现错误！");
        return -1;
    }

    string sn = getCPUSerial();
    string r = (sn == "17e1c1f6dbc0861c") ? "l" : "p";
    global.lp = r;

    if (r == "p")
    {
        exit(0);
    }

    // 设置回调
    setupCallbacks(decoder);

    // 启动线程
    startThreads();

    std::cout << "所有服务已经启动。" << std::endl;

    // 启动主监控和日志记录
    monitorAndLog(decoder);

    return 0;
}
