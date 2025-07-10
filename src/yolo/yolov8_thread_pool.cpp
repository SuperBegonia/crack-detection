
#include "yolov8_thread_pool.h"
#include "draw/cv_draw.h"
// 构造函数
ThreadPool::ThreadPool() { stop = false; }

// 析构函数
ThreadPool::~ThreadPool()
{
    // stop all threads
    stop = true;
    cv_task.notify_all();
    for (auto &thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}
// 初始化：加载模型，创建线程，参数：模型路径，线程数量
nn_error_e ThreadPool::startTPool(std::string &model_path, int num_threads)
{
    // 遍历线程数量，创建模型实例，放入vector
    // 这些线程加载的模型是同一个
    for (size_t i = 0; i < num_threads; ++i)
    {
        std::shared_ptr<Yolov8Detection> Yolov8 = std::make_shared<Yolov8Detection>();
        Yolov8->LoadModel(model_path.c_str());
        Yolov8_instances.push_back(Yolov8);
    }
    // 遍历线程数量，创建线程
    for (size_t i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(&ThreadPool::worker, this, i);
    }
    return NN_SUCCESS;
}

// 线程函数。参数：线程id
void ThreadPool::worker(int id)
{
    while (!stop)
    {
        // ID +
        std::pair<int, std::pair<std::chrono::time_point<std::chrono::system_clock>, cv::Mat>> task;
        std::shared_ptr<Yolov8Detection> instance = Yolov8_instances[id]; // 获取模型实例
        {
            // 获取任务
            std::unique_lock<std::mutex> lock(mtx1);
            cv_task.wait(lock, [&]
                         { return !tasks.empty() || stop; });

            if (stop)
            {
                return;
            }

            task = tasks.front();
            tasks.pop();
        }
        // 运行模型
        std::vector<Detection> detections;

        instance->Run(task.second.second, detections);
        {
            // 保存结果
            std::lock_guard<std::mutex> lock(mtx2);

            if (results.size() > 100)
            {
                results.erase(results.begin());
            }
            results.insert({task.first, detections});

            // 只发送结果 不进行绘制
            if (need_draw)
            {
                DrawDetections(task.second.second, detections, task.second.first, new_id, task.first);

                // 防止内存爆炸 ，移除最前面的图
                if (img_results.size() > 100)
                {
                    img_results.erase(img_results.begin());
                }
                img_results.insert({task.first, task.second});
            }

        }
    }
}
// 提交任务，参数：图片，id（帧号）
nn_error_e ThreadPool::addTask(const cv::Mat &img, int id)
{
    // 如果任务队列中的任务数量大于10，等待，避免内存占用过多
    while (tasks.size() > 80)
    {
        printf("\n---------------------------------- 任务数量过多 等待中----------------------\n");

        // sleep 1ms
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    {
        // 保存任务
        std::lock_guard<std::mutex> lock(mtx1);
        tasks.push({id, {std::chrono::system_clock::now(), img}});
    }
    cv_task.notify_one();
    return NN_SUCCESS;
}

// 获取结果，参数：检测框，id（帧号）
nn_error_e ThreadPool::getTargetResult(std::vector<Detection> &objects, int id)
{

    int loop_cnt = 0;
    // 如果没有结果，等待
    while (results.find(id) == results.end())
    {
        // sleep 1ms
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        loop_cnt++;
        if (loop_cnt > 1000)
        {
            NN_LOG_ERROR("getTargetResult timeout");
            return NN_TIMEOUT;
        }
    }
    std::lock_guard<std::mutex> lock(mtx2);
    objects = results[id];
    // remove from map
    results.erase(id);

    return NN_SUCCESS;
}

// 获取结果（图片），参数：图片，id（帧号）
nn_error_e ThreadPool::getTargetImgResult(cv::Mat &img, int id)
{
    int loop_cnt = 0;
    // 如果没有结果，等待
    while (img_results.find(id) == img_results.end())
    {
        // 等待 5ms x 1000 = 5s
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop_cnt++;
        if (loop_cnt > 1000)
        {
            NN_LOG_ERROR("getTargetImgResult timeout");
            return NN_TIMEOUT;
        }
    }
    std::lock_guard<std::mutex> lock(mtx2);
    img = img_results[id].second;
    // remove from map
    img_results.erase(id);

    return NN_SUCCESS;
}
// 停止所有线程
void ThreadPool::stopAll()
{
    stop = true;
    cv_task.notify_all();
}