#ifndef RKMPP_ENCODER_H
#define RKMPP_ENCODER_H

#include <iostream>
#include <functional>
#include <vector>
#include <fstream>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <rockchip/rk_type.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi.h>
#include <unistd.h>
#include <rga.h>
#include <im2d_type.h>
#include <im2d.h>

#include <rga.h>

#include "utils/rk_helper.cpp"

using namespace rk_helper;

struct H264_DATA
{
    uint8_t *data;
    int size;
};

class RKMPPEncoder
{
private:
    std::function<void(uint8_t *data, int size)> on_encoder_ok = nullptr;
    AVCodecContext *m_pCodecCtx = nullptr; // 编码器上下文
    const AVCodec *m_pCodec = nullptr;     // 编码器
    AVPacket *m_packet = nullptr;          // Packet
    AVFrame *m_pFrame = nullptr;           // 原始帧
    AVFrame *m_pFrameNV12 = nullptr;       // 转换后的帧
    SwsContext *m_swsContext = nullptr;    // 像素格式转换上下文
    AVPixelFormat m_src_format;            // 输入像素格式

    int m_in_w;
    int m_in_h;
    int64_t bit_rate;
    bool is_start = false;

    SafeQueue<cv::Mat> *m_mat_queue = nullptr;

    // 私有方法
    bool initializeEncoder();
    void cleanup();
    void matToAvFrame(const cv::Mat &mat, AVFrame *frame);
    AVFrame *convertToNV12(AVFrame *srcFrame);
    void matToNV12UsingRGA(const cv::Mat &mat, AVFrame *frame);

public:
    RKMPPEncoder(int w = 1920, int h = 1080, double rate = 1.0, AVPixelFormat format = AV_PIX_FMT_BGR24);
    ~RKMPPEncoder();

    bool init();
    void add_data(const cv::Mat &data);
    void encoder();
    void release();
    void set_on_encoder_ok_cb(std::function<void(uint8_t *data, int size)> cb);
};

// 构造函数
RKMPPEncoder::RKMPPEncoder(int w, int h, double rate, AVPixelFormat format)
    : m_src_format(format), m_in_w(w), m_in_h(h), bit_rate(static_cast<int64_t>(rate * 1000000.0))
{
    printf("bit_rate: %ld\n", bit_rate);
}

// 析构函数
RKMPPEncoder::~RKMPPEncoder()
{
    cleanup();
}

void RKMPPEncoder::cleanup()
{
    if (m_pFrame)
    {
        av_frame_free(&m_pFrame);
    }
    if (m_pFrameNV12)
    {
        av_frame_free(&m_pFrameNV12);
    }
    if (m_packet)
    {
        av_packet_free(&m_packet);
    }
    if (m_pCodecCtx)
    {
        avcodec_free_context(&m_pCodecCtx);
    }
    if (m_swsContext)
    {
        sws_freeContext(m_swsContext);
    }
    if (m_mat_queue)
    {
        delete m_mat_queue;
        m_mat_queue = nullptr;
    }
}

bool RKMPPEncoder::initializeEncoder()
{
    av_log_set_level(AV_LOG_INFO);

    // 查找编码器
    m_pCodec = avcodec_find_encoder_by_name("h264_rkmpp");
    if (!m_pCodec)
    {
        fprintf(stderr, "未找到编码器\n");
        return false;
    }

    // 创建编码器上下文
    m_pCodecCtx = avcodec_alloc_context3(m_pCodec);
    if (!m_pCodecCtx)
    {
        fprintf(stderr, "无法分配编码器上下文\n");
        return false;
    }

    // 设置编码器参数
    m_pCodecCtx->codec_id = m_pCodec->id;
    m_pCodecCtx->codec_type = m_pCodec->type;
    m_pCodecCtx->pix_fmt = AV_PIX_FMT_NV12; // 设置为 NV12
    m_pCodecCtx->width = m_in_w;
    m_pCodecCtx->height = m_in_h;
    m_pCodecCtx->time_base = AVRational{1, 30}; // 30 fps
    m_pCodecCtx->bit_rate = bit_rate;

    // 设置编码器选项
    AVDictionary *params = nullptr;
    av_dict_set(&params, "tune", "zerolatency", 0);
    av_dict_set(&params, "profile", "high", 0);

    // 打开编码器
    if (avcodec_open2(m_pCodecCtx, m_pCodec, &params) < 0)
    {
        fprintf(stderr, "无法打开编码器\n");
        av_dict_free(&params);
        return false;
    }
    av_dict_free(&params);

    // 分配原始帧
    m_pFrame = av_frame_alloc();
    if (!m_pFrame)
    {
        fprintf(stderr, "无法分配原始帧\n");
        return false;
    }
    m_pFrame->format = m_src_format; // 输入格式 (BGR24)
    m_pFrame->width = m_in_w;
    m_pFrame->height = m_in_h;

    if (av_frame_get_buffer(m_pFrame, 32) < 0)
    {
        fprintf(stderr, "无法分配原始帧数据\n");
        return false;
    }

    // 分配 NV12 格式的帧
    m_pFrameNV12 = av_frame_alloc();
    if (!m_pFrameNV12)
    {
        fprintf(stderr, "无法分配 NV12 帧\n");
        return false;
    }
    m_pFrameNV12->format = AV_PIX_FMT_NV12; // 编码器需要 NV12 格式
    m_pFrameNV12->width = m_in_w;
    m_pFrameNV12->height = m_in_h;

    if (av_frame_get_buffer(m_pFrameNV12, 32) < 0)
    {
        fprintf(stderr, "无法分配 NV12 帧数据\n");
        return false;
    }

    // 分配 Packet
    m_packet = av_packet_alloc();
    if (!m_packet)
    {
        fprintf(stderr, "无法分配 Packet\n");
        return false;
    }

    // 初始化 SwsContext，用于像素格式转换
    m_swsContext = sws_getContext(
        m_in_w, m_in_h, m_src_format,
        m_in_w, m_in_h, AV_PIX_FMT_NV12,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsContext)
    {
        fprintf(stderr, "无法初始化 SwsContext\n");
        return false;
    }

    return true;
}

bool RKMPPEncoder::init()
{
    m_mat_queue = new SafeQueue<cv::Mat>();
    m_mat_queue->set_max_capacity(128);

    if (!initializeEncoder())
    {
        fprintf(stderr, "编码器初始化失败\n");
        return false;
    }
    else
    {
        printf("编码器初始化成功\n");
        return true;
    }
}

void RKMPPEncoder::add_data(const cv::Mat &data)
{
    m_mat_queue->push(data);
}

void RKMPPEncoder::matToAvFrame(const cv::Mat &mat, AVFrame *frame)
{
    if (frame->width != mat.cols || frame->height != mat.rows || frame->format != m_src_format)
    {
        fprintf(stderr, "帧尺寸或格式不匹配\n");
        return;
    }

    int linesize = frame->linesize[0];
    int bytes_per_row = mat.cols * mat.elemSize();

    for (int y = 0; y < mat.rows; y++)
    {
        uint8_t *dst = frame->data[0] + y * linesize;
        const uint8_t *src = mat.ptr(y);
        memcpy(dst, src, bytes_per_row);
    }
}

AVFrame *RKMPPEncoder::convertToNV12(AVFrame *srcFrame)
{
    if (!srcFrame || !m_pFrameNV12)
    {
        fprintf(stderr, "源帧或目标帧为空\n");
        return nullptr;
    }

    // 转换为 NV12 格式
    int ret = sws_scale(
        m_swsContext,
        srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
        m_pFrameNV12->data, m_pFrameNV12->linesize);
    if (ret <= 0)
    {
        fprintf(stderr, "帧格式转换错误\n");
        return nullptr;
    }

    // 设置 PTS 等信息
    m_pFrameNV12->pts = srcFrame->pts;

    return m_pFrameNV12;
}

void RKMPPEncoder::encoder()
{
    is_start = true;
    printf("开始编码循环\n");
    while (is_start)
    {
        cv::Mat mat = m_mat_queue->pop();

        // 转换 cv::Mat 到 AVFrame (BGR24)
        matToNV12UsingRGA(mat, m_pFrameNV12);

        // 转换为 NV12 格式
        // AVFrame* nv12Frame = convertToNV12(m_pFrame);
        // if (!nv12Frame) {
        //     fprintf(stderr, "转换为 NV12 格式失败\n");
        //     continue;
        // }

        // 发送帧到编码器
        int ret = avcodec_send_frame(m_pCodecCtx, m_pFrameNV12);
        if (ret < 0)
        {
            fprintf(stderr, "发送帧到编码器时出错\n");
            continue;
        }

        // 从编码器接收 Packet
        while (ret >= 0)
        {
            ret = avcodec_receive_packet(m_pCodecCtx, m_packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                fprintf(stderr, "编码过程中出错\n");
                break;
            }

            // 调用回调函数
            if (on_encoder_ok)
            {
                try
                {
                    on_encoder_ok(m_packet->data, m_packet->size);
                }
                catch (const std::exception &e)
                {
                    fprintf(stderr, "回调函数异常: %s\n", e.what());
                }
            }

            av_packet_unref(m_packet);
        }
    }
    printf("编码循环结束\n");
}

void RKMPPEncoder::release()
{
    is_start = false;
    cleanup();
}

void RKMPPEncoder::set_on_encoder_ok_cb(std::function<void(uint8_t *data, int size)> cb)
{
    on_encoder_ok = cb;
}

void RKMPPEncoder::matToNV12UsingRGA(const cv::Mat &mat, AVFrame *frame)
{
    if (mat.empty() || !frame)
    {
        fprintf(stderr, "输入的 mat 或 frame 无效\n");
        return;
    }

    // 计算 NV12 所需的缓冲区大小
    size_t y_size = frame->width * frame->height;
    size_t uv_size = y_size / 2;
    size_t nv12_size = y_size + uv_size;

    // 分配一个临时缓冲区用于 RGA 转换
    uint8_t *nv12_buffer = (uint8_t *)malloc(nv12_size);
    if (!nv12_buffer)
    {
        fprintf(stderr, "内存分配失败\n");
        return;
    }

    // 创建目标图像的 rga_buffer_t，指向临时缓冲区
    rga_buffer_t dst = wrapbuffer_virtualaddr(nv12_buffer, frame->width, frame->height, RK_FORMAT_YCbCr_420_SP);

    // 创建源图像的 rga_buffer_t
    rga_buffer_t src = wrapbuffer_virtualaddr((void *)mat.data, mat.cols, mat.rows, RK_FORMAT_BGR_888);

    // 执行颜色空间转换
    IM_STATUS status = imcvtcolor(src, dst, src.format, dst.format);
    if (status != IM_STATUS_SUCCESS)
    {
        fprintf(stderr, "imcvtcolor error: %s\n", imStrError(status));
        free(nv12_buffer);
        return;
    }

    // 将转换后的数据拆分到 AVFrame 的数据平面
    memcpy(frame->data[0], nv12_buffer, y_size);           // 复制 Y 数据
    memcpy(frame->data[1], nv12_buffer + y_size, uv_size); // 复制 UV 数据

    // 释放临时缓冲区
    free(nv12_buffer);
}

#endif // RKMPP_ENCODER_H
