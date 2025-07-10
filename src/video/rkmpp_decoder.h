#pragma once
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
}
#include <libpng/png.h>

#include <opencv2/imgcodecs.hpp>
#include "utils/rk_helper.cpp"
#include "MPPDecoder.h"
#include "types/video_infos_type.h"
typedef void (*CallbackFunction)(unsigned char *data, int size, int w, int h, AVPixelFormat format, void *handler);
typedef void (*CallbackFunctionAVFrame)(AVFrame *frame, void *handler);
typedef void (*CallbackFunctionMat)(cv::Mat mat, video_decoder_info, void *handler);
typedef void (*StringCallback)(const char *);

using namespace rk_helper;

namespace FCourier
{
    class RKMPPDecoder
    {
        struct RawData
        {
            uint8_t *data;
            size_t size;
        };

    private:
        /// <summary>
        /// 原始数据队列,用于存放原始的数据
        /// </summary>
        SafeQueue<uint8_t> _raw_queue;
        SafeQueue<AVPacket *> _avpacke_queue;
        const AVCodec *_codec = nullptr;
        AVCodecContext *_ctx = nullptr;
        bool matReady = false;
        int _frame_count;
        AVFrame *_frame = nullptr;
        AVPacket _avpkt;
        bool _is_init = false;
        bool _is_start = false;
        AVFormatContext *_input_format_context = nullptr;
        int _video_stream_index = -1;
        CallbackFunction _raw_cb = nullptr;
        CallbackFunctionAVFrame _avframe_cb = nullptr;
        CallbackFunctionMat _mat_cb = nullptr;
        SwsContext *_swsContext = nullptr;
        AVFrame *_target_frame = nullptr;
        FPSCalculator _fps_calculator;
        void *_object_instance = nullptr;
        cv::Mat pCvMat;
        string rtsp_url_;
        string rtsp_type_;
        bool is_use_rtps_ = false;
        uint8_t *yuv_buf = nullptr;
        size_t yuv_size = 0;
        uint8_t *nv12_buf = nullptr;
        size_t nv12_size = 0;

    public:
        RKMPPDecoder()
        {
        }
        ~RKMPPDecoder()
        {
            if (nv12_buf)
            {
                delete[] nv12_buf;
                nv12_buf = nullptr;
            }
            if (yuv_buf)
            {
                delete[] yuv_buf;
                yuv_buf = nullptr;
            }
            if (_frame)
            {
                av_frame_free(&_frame);
            }
            if (_ctx)
            {
                avcodec_free_context(&_ctx);
            }
            if (_input_format_context)
            {
                avformat_close_input(&_input_format_context);
            }
        }
        void OnError(std::string msg)
        {
            printf("Error: %s\n", msg.c_str());
        }

        bool init()
        {
            if (_is_init)
            {
                return true;
            }
            std::cout << "初始化完成" << std::endl;
            // 初始化AVPacket
            av_init_packet(&_avpkt);
            _frame = av_frame_alloc();
            if (!_frame)
            {
                OnError("无法分配视频帧");
                return false;
            }
            //_frame->format = AV_PIX_FMT_BGR24;
            //_frame_count = 0;
            _is_init = true;
            return true;
        }

        void set_callback(CallbackFunction cb)
        {
            _raw_cb = cb;
        }
        void set_avframe_callback(CallbackFunctionAVFrame cb)
        {
            _avframe_cb = cb;
        }
        void set_mat_callback(CallbackFunctionMat cb)
        {
            _mat_cb = cb;
        }

        void set_object_instance(void *handler)
        {
            _object_instance = handler;
        }

        int get_fps()
        {
            _fps_calculator.Update();
            return _fps_calculator.getFramePerSecond();
        }

        void set_raw_data(uint8_t *inputbuf, size_t size)
        {
            // 将数据放入队列
            _raw_queue.push(inputbuf, size);
        }

        void start()
        {
            _is_start = true;
            init();
            // 创建一个线程用于解码
            std::thread decode_thread(&RKMPPDecoder::Decode, this);
            decode_thread.detach();

            // 创建一个线程用于接收数据
            std::thread receive_thread(&RKMPPDecoder::StartIOHandler, this);
            receive_thread.detach();
        }

        void start(string url, string type)
        {
            _is_start = true;
            this->rtsp_url_ = url;
            this->rtsp_type_ = type;
            is_use_rtps_ = true;
            init();
            // 创建一个线程用于解码
            std::thread decode_thread(&RKMPPDecoder::Decode, this);
            decode_thread.detach();

            // 创建一个线程用于接收数据
            std::thread receive_thread(&RKMPPDecoder::StartIOHandler, this);
            receive_thread.detach();
        }

        void play()
        {
        }

        void stop()
        {
            _is_start = false;
            _raw_queue.exit();
            _avpacke_queue.exit();
        }

        /// <summary>
        /// 将自定义的数据流放入AVIOContext
        /// </summary>
        void StartIOHandler()
        {

            printf("IOHandler 线程启动\n");
            AVDictionary *avfmtOps = NULL;
            AVFormatContext *tmp_ctx = avformat_alloc_context();
            AVIOContext *avio; // 自定义IO输入
            int ret;

            if (is_use_rtps_)
            {
                AVInputFormat *informat = NULL;
                av_dict_set(&avfmtOps, "rtsp_transport", rtsp_type_.c_str(), 0);
                ret = avformat_open_input(&tmp_ctx, rtsp_url_.c_str(), informat, &avfmtOps);
            }
            else
            {
                int IOBufferSize = 1 * 1024;
                uint8_t *IOBuffer = (uint8_t *)av_malloc(IOBufferSize + AV_INPUT_BUFFER_PADDING_SIZE);
                avio = avio_alloc_context(IOBuffer, IOBufferSize, 0, &_raw_queue, [](void *opaque, uint8_t *buf, int buf_size)
                                          {
                    SafeQueue<uint8_t>* queue = (SafeQueue<uint8_t>*)opaque;
                    if (queue->is_exit()) {
                        return AVERROR_EOF;
                    }
                    // 从队列中取出数据
                    while (queue->empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        if (queue->is_exit()) {
                            return AVERROR_EOF;
                        }
                    }
                    int size = queue->size();
                    if (size > buf_size) {
                        size = buf_size;
                    }

                    for (int i = 0; i < size; i++) {
                        buf[i] = queue->pop();
                    }
                    return size; }, NULL, NULL);

                tmp_ctx->pb = avio;
                tmp_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
                tmp_ctx->iformat = av_find_input_format("h264");
                AVDictionary *avfmtOps = NULL;
                ret = avformat_open_input(&tmp_ctx, "", NULL, &avfmtOps);
            }
            av_dict_free(&avfmtOps);

            if (ret < 0)
            {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                OnError("avformat_open_input 失败");
                // 打印错误
                std::cerr << "错误: " << errbuf << std::endl;

                return;
            }
            av_dict_free(&avfmtOps);
            _input_format_context = tmp_ctx;
            ret = avformat_find_stream_info(_input_format_context, NULL);
            if (ret < 0)
            {
                OnError("avformat_find_stream_info 失败");
                return;
            }
            _video_stream_index = -1;

            // 查找视频流
            for (int i = 0; i < _input_format_context->nb_streams; i++)
            {
                if (_input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                {
                    _video_stream_index = i;
                    _codec = avcodec_find_decoder_by_name("h264_rkmpp");

                    printf("codec id:%d\n", _input_format_context->streams[i]->codecpar->codec_id);
                    // 获取 codec_id 的名称
                    const char *codec_name = avcodec_get_name(_input_format_context->streams[i]->codecpar->codec_id);
                    printf("codec name:%s\n", codec_name);
                    if (!_codec)
                    {
                        OnError("未找到解码器");
                        return;
                    }

                    // 创建解码器上下文
                    _ctx = avcodec_alloc_context3(_codec);
                    if (!_ctx)
                    {
                        OnError("无法分配视频解码器上下文");
                        return;
                    }

                    // 将解码器参数拷贝到解码器上下文
                    if (avcodec_parameters_to_context(_ctx, _input_format_context->streams[i]->codecpar) < 0)
                    {
                        OnError("将解码器参数复制到输入解码器上下文失败");
                        return;
                    }
                    _ctx->pkt_timebase = _input_format_context->streams[i]->time_base;
                    break;
                }
            }
            if (_video_stream_index == -1)
            {
                OnError("无法找到视频流");
                return;
            }
            _ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            // 低延迟模式
            _ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
            _ctx->flags |= AV_CODEC_FLAG2_FAST;
            AVDictionary *avCtxOps = NULL;
            av_dict_set(&avCtxOps, "threads", "auto", 0);
            ret = avcodec_open2(_ctx, _codec, &avCtxOps);
            av_dict_free(&avCtxOps);
            avCtxOps = nullptr;
            if (ret < 0)
            {
                OnError("无法打开解码器");
                return;
            }

            // 创建一个线程用于接收数据
            std::thread receive_thread(&RKMPPDecoder::AsyncReceivingPackets, this);
            receive_thread.detach();
        }

        void AsyncReceivingPackets()
        {
            try
            {
                printf("AsyncReceivingPackets 线程启动\n");
                while (_is_start)
                {
                    int ret;
                    try
                    {
                        // 创建一个 AVPacket
                        AVPacket *avpkt = av_packet_alloc();
                        av_init_packet(avpkt);
                        ret = av_read_frame(_input_format_context, avpkt);
                        if (ret < 0)
                        {
                            char errbuf[AV_ERROR_MAX_STRING_SIZE];
                            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                            OnError("av_read_frame 失败");
                            av_packet_free(&avpkt);
                            continue;
                        }
                        else if (ret != 0)
                        {
                            av_packet_unref(avpkt);
                            av_packet_free(&avpkt);
                            continue;
                        }

                        // 将数据放入队列
                        _avpacke_queue.push(avpkt);
                    }
                    catch (std::exception &e)
                    {
                        ret = -1;
                        printf("AsyncReceivingPackets 异常:%s\n", e.what());
                        // 释放 AVPacket
                        // 确保即使发生异常也能释放资源
                    }
                }
            }
            catch (std::exception &e)
            {
                printf("AsyncReceivingPackets 外部异常:%s\n", e.what());
            }
        }

        std::string getCurrentTimestamp()
        {
            std::time_t now = std::time(nullptr);
            std::tm *local_time = std::localtime(&now);
            char buffer[80];
            std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", local_time);
            return std::string(buffer);
        }

        // Function to save cv::Mat as JPG with timestamp filename
        bool saveMatAsJPGWithTimestamp(const cv::Mat mat)
        {
            if (mat.empty())
            {
                std::cerr << "空图像。无法保存。" << std::endl;
                return false;
            }

            std::string timestamp = getCurrentTimestamp();
            std::string filename = "image_" + timestamp + ".jpg";

            if (cv::imwrite(filename, mat))
            {
                std::cout << "图像已保存为 " << filename << std::endl;
                return true;
            }
            else
            {
                std::cerr << "保存图像失败。" << std::endl;
                return false;
            }
        }
        bool NV12ToMatUsingOpenCV(const AVFrame *frame, cv::Mat &mat)
        {
            if (!frame)
            {
                std::cerr << "无效的 AVFrame 指针\n";
                return false;
            }

            // 获取宽度和高度
            int width = frame->width;
            int height = frame->height;

            // 计算 NV12 数据的大小
            int ySize = width * height;
            int uvSize = width * height / 2;
            int frameSize = ySize + uvSize;
            if (nv12_size != frameSize)
            {
                nv12_size = frameSize;

                if (nv12_buf == nullptr)
                {
                    std::cerr << "初始化 NV12 缓冲区!\n";
                    nv12_buf = new uint8_t[nv12_size];
                }
                else
                {
                    std::cerr << "重新初始化 NV12 缓冲区!\n";
                    delete[] nv12_buf;
                    nv12_buf = new uint8_t[nv12_size];
                }
            }

            // 正确复制 Y 平面数据，考虑 linesize
            for (int y = 0; y < height; y++)
            {
                memcpy(nv12_buf + y * width, frame->data[0] + y * frame->linesize[0], width);
            }

            // 正确复制 UV 平面数据，考虑 linesize
            for (int y = 0; y < height / 2; y++)
            {
                memcpy(nv12_buf + ySize + y * width, frame->data[1] + y * frame->linesize[1], width);
            }

            // 创建 OpenCV Mat 对象用于 NV12 数据
            cv::Mat nv12(height + height / 2, width, CV_8UC1, nv12_buf);

            // 计算 BGR888 数据的大小
            auto size = width * height * 3; // For BGR888

            if (size != yuv_size)
            {
                yuv_size = size;

                if (yuv_buf == nullptr)
                {
                    std::cerr << "初始化 YUV 缓冲区!\n";
                    yuv_buf = new uint8_t[yuv_size];
                }
                else
                {
                    std::cerr << "重新初始化 YUV 缓冲区!\n";
                    delete[] yuv_buf;
                    yuv_buf = new uint8_t[yuv_size];
                }
            }

            // 使用 OpenCV 进行 NV12 到 BGR888 的颜色转换
            try
            {
                cv::Mat bgr(height, width, CV_8UC3, yuv_buf);
                cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);
                mat = bgr.clone();

                if (!mat.empty())
                {
                    return true; // 转换成功
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "创建 OpenCV Mat 对象失败!\n";
                std::cerr << e.what() << '\n';
            }

            std::cerr << "颜色格式转换失败\n";
            return false; // 转换失败
        }

        bool NV12ToMatUsingRGA1(const AVFrame *frame, cv::Mat &mat)
        {
            if (!frame)
            {
                std::cerr << "无效的 AVFrame 指针\n";
                return false;
            }

            // 获取宽度和高度
            int width = frame->width;
            int height = frame->height;
            // printf("h %d, w: %d \n", height, width);

            // 计算 NV12 数据的大小
            int ySize = width * height;
            int uvSize = width * height / 2;
            int frameSize = ySize + uvSize;
            if (nv12_size != frameSize)
            {
                nv12_size = frameSize;

                if (nv12_buf == nullptr)
                {
                    std::cerr << "初始化 NV12 缓冲区!\n";
                    nv12_buf = new uint8_t[nv12_size];
                }
                else
                {
                    std::cerr << "重新初始化 NV12 缓冲区!\n";
                    delete[] nv12_buf;
                    nv12_buf = new uint8_t[nv12_size];
                }
            }

            // 正确复制 Y 平面数据，考虑 linesize
            for (int y = 0; y < height; y++)
            {
                memcpy(nv12_buf + y * width, frame->data[0] + y * frame->linesize[0], width);
            }

            // 正确复制 UV 平面数据，考虑 linesize
            for (int y = 0; y < height / 2; y++)
            {
                memcpy(nv12_buf + ySize + y * width, frame->data[1] + y * frame->linesize[1], width);
            }

            // 计算 BGR888 数据的大小
            auto size = width * height * 3; // For BGR888

            if (size != yuv_size)
            {
                yuv_size = size;

                if (yuv_buf == nullptr)
                {
                    std::cerr << "初始化 YUV 缓冲区!\n";
                    yuv_buf = new uint8_t[yuv_size];
                }
                else
                {
                    std::cerr << "重新初始化 YUV 缓冲区!\n";
                    delete[] yuv_buf;
                    yuv_buf = new uint8_t[yuv_size];
                }
            }

            // NV12 到 BGR888 转换
            bool is_ok = rga_color_cov(RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888, width, height, nv12_buf, &yuv_buf, yuv_size);
            if (is_ok)
            {
                try
                {
                    mat = cv::Mat(height, width, CV_8UC3, yuv_buf).clone();
                    if (!mat.empty())
                    {
                        return true; // 转换成功
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "创建 OpenCV Mat 对象失败!\n";
                    std::cerr << e.what() << '\n';
                }
            }
            else
            {
                std::cerr << "颜色格式转换失败\n";
            }

            // 清理
            // 注意：不要在这里删除 yuv_buf，因为它可能在下次调用时需要
            return false; // 转换失败
        }
        // Function to generate a random filename
        void generate_random_filename(char *filename, size_t size)
        {
            const char *prefix = "frame_";
            const char *suffix = ".png";
            snprintf(filename, size, "%s%d%s", prefix, rand(), suffix);
        }

        void Decode()
        {
            printf("Decode 线程启动\n");
            SwsContext *conversion = nullptr;
            while (_is_start)
            {
                auto decode_start_time = std::chrono::high_resolution_clock::now(); // 记录开始时间

                auto avpkt = _avpacke_queue.pop();

                if (avpkt && avpkt->size > 0)
                {
                    int ret = avcodec_send_packet(_ctx, avpkt);

                    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                    {
                        char errbuf[AV_ERROR_MAX_STRING_SIZE];
                        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                        std::cout << "avcodec_send_packet 错误代码: " << ret << " 信息: " << errbuf << std::endl;
                        OnError("发送解码包时出错");
                        av_packet_unref(avpkt);
                        av_packet_free(&avpkt);
                        continue;
                    }

                    if (ret < 0)
                    {
                        OnError("发送解码包时出错");
                        av_packet_unref(avpkt);
                        av_packet_free(&avpkt);
                        continue;
                    }
                    while (ret >= 0)
                    {
                        ret = avcodec_receive_frame(_ctx, _frame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        {
                            break;
                        }
                        else if (ret < 0)
                        {
                            OnError("解码过程中出错");
                            break;
                        }

                        auto decode_end_time = std::chrono::high_resolution_clock::now(); // 记录结束时间
                        auto decoding_delay = std::chrono::duration_cast<std::chrono::milliseconds>(decode_end_time - decode_start_time).count();
                        //std::cout << "Decoding delay: " << decoding_delay << " ms" << std::endl;

                        // save_frame_as_png(_frame);
                        _fps_calculator.CountAFrame();

                        if (_mat_cb)
                        {
                            // 创建一个 Mat 并自动获取分辨率
                            cv::Mat yuvImg;
                            if (NV12ToMatUsingOpenCV(_frame, yuvImg))
                            {
                                video_decoder_info info;
                                info.decoder_delay = decoding_delay;
                                info.fps = _fps_calculator.getFramePerSecond();
                                info.width = _frame->width;
                                info.height = _frame->height;
                                _mat_cb(yuvImg, info,_object_instance);
                            }
                            else
                            {
                                OnError("NV12 到 Mat 转换失败");
                            }
                        }
                    }
                    av_packet_unref(avpkt); // 释放当前 AVPacket 资源
                    av_packet_free(&avpkt);
                }
            }
        }
    };
}
