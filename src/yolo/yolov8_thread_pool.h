

#ifndef RK3588_DEMO_Yolov8_THREAD_POOL_H
#define RK3588_DEMO_Yolov8_THREAD_POOL_H

#include "Yolov8Detection.h"

#include <iostream>
#include <vector>
#include <queue>
#include <map>
#include <thread>
#include <mutex>
#include <ctime>
#include <ratio>
#include <chrono>

#include <condition_variable>
#include "types/video_infos_type.h"


typedef std::chrono::time_point<std::chrono::system_clock> fc_clock;

class ThreadPool
{
private:
    // 帧ID  + 时间 + mat
    std::queue<std::pair<int, std::pair<std::chrono::time_point<std::chrono::system_clock> ,cv::Mat>>> tasks;             // <id, img>用来存放任务
    std::vector<std::shared_ptr<Yolov8Detection>> Yolov8_instances; // 模型实例
    std::map<int, std::vector<Detection>> results;         // <id, objects>用来存放结果（检测框）
    std::map<int, std::pair<std::chrono::time_point<std::chrono::system_clock> ,cv::Mat>> img_results;                    // <id, img>用来存放结果（图片）
    std::vector<std::thread> threads;                      // 线程池
    std::mutex mtx1;
    std::mutex mtx2;
    std::condition_variable cv_task;
    bool stop;
    
    void worker(int id);

public:
    ThreadPool();  // 构造函数
    ~ThreadPool(); // 析构函数

    nn_error_e startTPool(std::string &model_path, int num_threads = 12);     // 初始化
    nn_error_e addTask(const cv::Mat &img, int id);                   // 提交任务
    nn_error_e getTargetResult(std::vector<Detection> &objects, int id); // 获取结果（检测框）
    nn_error_e getTargetImgResult(cv::Mat &img, int id);   
    bool need_draw = false;              // 获取结果（图片）
    void stopAll();    
    int new_id = 0;                                                  // 停止所有线程
};

#endif // RK3588_DEMO_Yolov8_THREAD_POOL_H
