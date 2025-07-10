// 只包含一次
#ifndef RK_HELPER_H
#define RK_HELPER_H
#include <fstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>
#include <sstream>
#include <unistd.h>
#include <iterator>
#include <random>

#include <rockchip/rk_type.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi.h>
#include <unistd.h>
#include <opencv2/imgproc.hpp>
#include <rga.h>
#include <im2d_type.h>
#include <im2d.h>

using namespace std;
namespace rk_helper
{
    struct NpuLoad
    {
        int core0;
        int core1;
        int core2;
    };
    struct CPUStats
    {
        unsigned long long user;
        unsigned long long nice;
        unsigned long long system;
        unsigned long long idle;
        unsigned long long iowait;
        unsigned long long irq;
        unsigned long long softirq;
        unsigned long long steal;
        unsigned long long guest;
        unsigned long long guest_nice;
    };
    struct rk_helper_data
    {
        char *data;
        int size;
    };
    struct MemoryInfo
    {
        long totalMemory; // 总内存
        long freeMemory;  // 空闲内存
        long buffers;     // 缓冲区
        long cached;      // 缓存
    };

    // 增加异或加密函数



    static int generateRandomNumber(int min, int max)
    {
        // 创建一个随机数引擎，使用随机设备作为种子
        random_device rd;
        mt19937 gen(rd());

        // 创建一个均匀分布，范围从 min 到 max
        uniform_int_distribution<> dis(min, max);

        // 返回生成的随机数
        return dis(gen);
    }
    static string getCPUSerial()
    {
        ifstream file("/proc/cpuinfo");
        if (!file.is_open())
        {
            cerr << "无法打开 /proc/cpuinfo 文件" << endl;
            return "";
        }

        string line;
        while (getline(file, line))
        {
            // 查找包含 "Serial" 的行
            if (line.find("Serial") != string::npos)
            {
                size_t pos = line.find(":");
                if (pos != string::npos)
                {
                    // 提取冒号后面的字符串
                    string serial = line.substr(pos + 1);
                    // 去除前导空格
                    serial.erase(0, serial.find_first_not_of(" \t"));
                    // 去除尾部空格
                    serial.erase(serial.find_last_not_of(" \t") + 1);
                    return serial;
                }
            }
        }
        return "";
    }

    /*
        这是利用rga 转换颜色格式的函数
        输入 src_data 数据
        输入 src_format 数据格式
        输入 dst_format 目标数据格式
        输入 width 宽度
        输入 height 高度
        输入 output_buffer 输出缓冲区
        输入 output_buffer_size 输出缓冲区大小
        返回 true 表示转换成功
        返回 false 表示转换失败
        需要注意的是，这里的输出缓冲区的内存是由外部管理的，但是一旦调用的时候，内部发现
        输出缓冲区的内存不足，会自动分配内存，并返回新的地址，并且原来的地址会被释放
    */
    static bool rga_color_cov(_Rga_SURF_FORMAT src_format, _Rga_SURF_FORMAT dst_format, int width, int height, RK_U8 *src_data, uint8_t **output_buffer, size_t &output_buffer_size)
    {
        im_rect src_rect;
        im_rect dst_rect;
        memset(&src_rect, 0, sizeof(src_rect));
        memset(&dst_rect, 0, sizeof(dst_rect));
        rga_buffer_t src = wrapbuffer_virtualaddr((void *)src_data, width, height, src_format);

        size_t dstSize = width * height * 3;

        if (dstSize != output_buffer_size)
        {
            if (*output_buffer != nullptr)
            {
                delete[] *output_buffer;
            }
            *output_buffer = new uint8_t[dstSize];
            output_buffer_size = dstSize;
        }

        rga_buffer_t dst = wrapbuffer_virtualaddr(*output_buffer, width, height, dst_format);

        auto ret = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888);

        if (ret == IM_STATUS_SUCCESS)
        {
            return true;
        }
        return false;
    }

    class FPSCalculator
    {
    private:
        int _LastSecond;
        int _FrameCount;
        double _FramePerSecond;

    public:
        FPSCalculator() : _LastSecond(-1), _FrameCount(0), _FramePerSecond(0) {}

        void CountAFrame()
        {
            _FrameCount++;
            int nowSecond = time(0);
            if (nowSecond != _LastSecond)
            {
                _FramePerSecond = _FrameCount;
                _FrameCount = 0;
                _LastSecond = nowSecond;
            }
        }

        void CountFrames(int count)
        {
            _FrameCount += count;
            int nowSecond = time(0);
            if (nowSecond != _LastSecond)
            {
                _FramePerSecond = _FrameCount;
                _FrameCount = 0;
                _LastSecond = nowSecond;
            }
        }

        void Update()
        {
            int nowSecond = time(0);
            if (nowSecond != _LastSecond)
            {
                _FramePerSecond = _FrameCount;
                _FrameCount = 0;
                _LastSecond = nowSecond;
            }
        }

        double getFramePerSecond() const
        {

            return _FramePerSecond;
        }

        void Restore()
        {
            _LastSecond = -1;
            _FrameCount = 0;
            _FramePerSecond = 0;
        }
    };

    template <typename T>
    class SafeQueue
    {
    private:
        std::queue<T> queue_;
        mutable std::mutex mutex_;
        std::condition_variable cond_;
        uint64_t max_capacity_ = 2048 * 1024; // 默认最大容量为2M
        bool exit_ = false;

    public:
        SafeQueue() {}
        SafeQueue(int c)
        {
            max_capacity_ = c;
        }

        void push(T value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= max_capacity_)
            {
                printf("queue is full, pop one\n");
                queue_.pop();
            }
            queue_.push(value);
            cond_.notify_one();
        }

        void push(T *data, size_t size)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= max_capacity_)
            {
                queue_.pop();
            }
            for (int i = 0; i < size; i++)
            {
                queue_.push(data[i]);
            }
            cond_.notify_one();
        }

        T pop()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (queue_.empty())
            {
                cond_.wait(lock);
            }
            T value = queue_.front();
            queue_.pop();
            return value;
        }

        bool empty()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.empty();
        }

        int size()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!queue_.empty())
            {
                queue_.pop();
            }
        }

        void exit()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cond_.notify_all();
            exit_ = true;
        }
        /// <summary>
        /// 设置最大容量，超过最大容量时，会将队列头部的数据移除
        /// </summary>
        /// <param name="max"></param>
        void set_max_capacity(uint64_t max)
        {
            max_capacity_ = max;
        }

        bool is_exit()
        {
            return exit_;
        }
    };

    class RK3588_HW_RUNING_STATTUS
    {

    private:
        std::mutex mutex_;

        NpuLoad RK3588_NPU_LOAD;
        float cpuUtilization;
        MemoryInfo memInfo;
        float calculateTotalCpuUtilization(const CPUStats &stats)
        {
            unsigned long long idle = stats.idle + stats.iowait;
            unsigned long long nonIdle = stats.user + stats.nice + stats.system +
                                         stats.irq + stats.softirq + stats.steal;

            unsigned long long total = idle + nonIdle;

            return (1.0f - static_cast<float>(idle) / total) * 100.0f;
        }
        CPUStats getCPUStats()
        {
            std::ifstream file("/proc/stat");
            CPUStats stats = {0};

            if (!file.is_open())
            {
                std::cerr << "Failed to open /proc/stat" << std::endl;
                return stats;
            }

            std::string line;
            while (std::getline(file, line))
            {
                if (line.substr(0, 3) == "cpu")
                {
                    std::istringstream iss(line);
                    std::vector<std::string> tokens(std::istream_iterator<std::string>{iss},
                                                    std::istream_iterator<std::string>());

                    if (tokens.size() < 5)
                    {
                        continue;
                    }

                    stats.user = std::stoull(tokens[1]);
                    stats.nice = std::stoull(tokens[2]);
                    stats.system = std::stoull(tokens[3]);
                    stats.idle = std::stoull(tokens[4]);
                    stats.iowait = std::stoull(tokens[5]);
                    stats.irq = std::stoull(tokens[6]);
                    stats.softirq = std::stoull(tokens[7]);
                    stats.steal = std::stoull(tokens[8]);
                    stats.guest = std::stoull(tokens[9]);
                    stats.guest_nice = std::stoull(tokens[10]);

                    break;
                }
            }

            file.close();
            return stats;
        }
        NpuLoad getNpuLoad(const std::string filePath = "/sys/kernel/debug/rknpu/load")
        {
            NpuLoad load = {0, 0, 0};
            std::ifstream file(filePath);

            if (!file.is_open())
            {
                std::cerr << "Failed to open file: " << filePath << std::endl;
                return load;
            }

            std::string line;
            while (std::getline(file, line))
            {
                std::istringstream iss(line);
                std::string token;

                while (iss >> token)
                {
                    if (token.find("Core0:") != std::string::npos)
                    {
                        iss >> load.core0;
                        iss.ignore(1, '%'); // Ignore the percentage sign
                    }
                    else if (token.find("Core1:") != std::string::npos)
                    {
                        iss >> load.core1;
                        iss.ignore(1, '%'); // Ignore the percentage sign
                    }
                    else if (token.find("Core2:") != std::string::npos)
                    {
                        iss >> load.core2;
                        iss.ignore(1, '%'); // Ignore the percentage sign
                    }
                }
            }

            file.close();
            return load;
        }

        // Function to get memory information from /proc/meminfo
        MemoryInfo getMemoryInfo()
        {
            MemoryInfo memInfo = {0, 0, 0, 0};
            std::ifstream file("/proc/meminfo");

            if (!file.is_open())
            {
                std::cerr << "Failed to open /proc/meminfo" << std::endl;
                return memInfo;
            }

            std::string line;
            while (std::getline(file, line))
            {
                std::istringstream iss(line);
                std::string key;
                long value;

                iss >> key >> value;
                if (key == "MemTotal:")
                {
                    memInfo.totalMemory = value;
                }
                else if (key == "MemFree:")
                {
                    memInfo.freeMemory = value;
                }
                else if (key == "Buffers:")
                {
                    memInfo.buffers = value;
                }
                else if (key == "Cached:")
                {
                    memInfo.cached = value;
                }
            }

            file.close();
            return memInfo;
        }

        // Function to print memory information
        void printMemoryInfo(const MemoryInfo &memInfo)
        {
            std::cout << "Memory Information:" << std::endl;
            std::cout << "Total Memory: " << memInfo.totalMemory / 1024 << " MB" << std::endl; // Convert to MB
            std::cout << "Free Memory: " << memInfo.freeMemory / 1024 << " MB" << std::endl;   // Convert to MB
            std::cout << "Buffers: " << memInfo.buffers / 1024 << " MB" << std::endl;          // Convert to MB
            std::cout << "Cached: " << memInfo.cached / 1024 << " MB" << std::endl;            // Convert to MB
        }

    public:
        static RK3588_HW_RUNING_STATTUS RK_HW_INFO;
        int n0, n1, n2 = 0;
        const NpuLoad get_npu_load()
        {
            return RK3588_NPU_LOAD;
        }
        const float get_cpu_utilization()
        {
            return cpuUtilization;
        }
        const MemoryInfo get_mem_info()
        {
            return memInfo;
        }

        /// @brief 所有信息刷新一次
        void GetAllOnce()
        {
            usleep(1000000); // Sleep for 1 second

            CPUStats stats1 = getCPUStats();
            usleep(1000000); // Sleep for 1 second
            CPUStats stats2 = getCPUStats();
            std::lock_guard<std::mutex> lock(mutex_);

            cpuUtilization = calculateTotalCpuUtilization(stats2) - calculateTotalCpuUtilization(stats1);

            RK3588_NPU_LOAD = getNpuLoad();
            n0 = 1;
            n1 = 2;
            n2 = 3;

            memInfo = getMemoryInfo();
        }
    };

}
#endif