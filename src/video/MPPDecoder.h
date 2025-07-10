#ifndef _H264MPPDECODER_H_
#define _H264MPPDECODER_H_

#include <iostream>
#include <functional>
#include <vector>
#include <opencv2/core.hpp>
extern "C"
{
#include <libavcodec/avcodec.h>
}

#include <rockchip/rk_type.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi.h>
#include <unistd.h>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <rga.h>
#include <im2d_type.h>
#include <im2d.h>

class H264MPPDecoder
{
private:
    MppCtx ctx{nullptr};  ///< 解码器句柄
    MppApi *mpi{nullptr}; ///< 解码器实例化对象指针
    // MppPacket mppPacket{nullptr};	///< 解码器输入的包结构
    // MppFrame  frame{nullptr};	///< 解码器输出的帧结构
    int width;                                       ///< 视频宽度
    int height;                                      ///< 视频高度
    std::function<void(cv::Mat yuvImg)> yuvCallBack; ///< 解码回調
    uint8_t *yuv_buf = nullptr;                      ///<  格式转换缓冲
    size_t yuv_size = 0;                             ///< 缓冲区大小
public:
    /*
        这是利用rga 转换颜色格式的函数
        输入 src_data 数据
        输入 src_format 数据格式
        输入 dst_format 目标数据格式
        输入 width 宽度
        输入 height 高度
        输入 output_buffer 缓冲区的变量的指针(可能会修改缓冲区地址，所以是二级指针)
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
            std::cout << "创建新的缓冲区" << std::endl;
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
    bool rga_color_cov(_Rga_SURF_FORMAT src_format, _Rga_SURF_FORMAT dst_format, int width, int height, RK_U8 *src_data)
    {
        im_rect src_rect;
        im_rect dst_rect;
        memset(&src_rect, 0, sizeof(src_rect));
        memset(&dst_rect, 0, sizeof(dst_rect));
        rga_buffer_t src = wrapbuffer_virtualaddr((void *)src_data, width, height, src_format);

        size_t dstSize = width * height * 3;
        if (dstSize != yuv_size)
        {
            std::cout << "创建新的缓冲区" << std::endl;
            if (yuv_buf != nullptr)
            {
                delete[] yuv_buf;
            }
            yuv_buf = new uint8_t[dstSize];
            yuv_size = dstSize;
        }

        rga_buffer_t dst = wrapbuffer_virtualaddr(yuv_buf, width, height, dst_format);

        auto ret = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888);

        if (ret == IM_STATUS_SUCCESS)
        {
            return true;
        }
        return false;
    }
    H264MPPDecoder(int width, int height)
    {
        this->width = width;
        this->height = height;
    }
    ~H264MPPDecoder();
    bool init()
    {
        MPP_RET ret = MPP_OK;
        MpiCmd mpi_cmd = MPP_CMD_BASE;
        MppParam param = NULL;
        MppCodingType type = MPP_VIDEO_CodingAVC;
        RK_U32 need_split = 1;

        // 创建MPP context和MPP api 接口
        ret = mpp_create(&ctx, &mpi);
        if (MPP_OK != ret)
        {
            std::cout << "mpp_create faild" << std::endl;
            stopDecode();
            return false;
        }
        // 配置解码器 解码文件需要 split 模式
        mpi_cmd = MPP_DEC_SET_PARSER_SPLIT_MODE;
        param = &need_split;
        ret = mpi->control(ctx, mpi_cmd, param);
        if (MPP_OK != ret)
        {
            std::cout << "split control faild" << std::endl;
            stopDecode();
            return false;
        }
        // 初始化 MPP
        ret = mpp_init(ctx, MPP_CTX_DEC, type);
        if (MPP_OK != ret)
        {
            std::cout << "mpp_init faild" << ret << std::endl;
            stopDecode();
            return false;
        }

        return true;
    }
    void startDecode3(AVPacket *packet)
    {
        MppPacket mppPacket = NULL;
        MppFrame frame = NULL;

        // 初始化MPP包
        mpp_packet_init(&mppPacket, packet->data, packet->size);
        mpp_packet_set_pts(mppPacket, packet->pts);
        if (MPP_OK != mpi->decode_put_packet(ctx, mppPacket))
        {
            printf("获取mppPacket 失败!\n");

            return;
        }

        // 获取解码后的帧
        if (MPP_OK != mpi->decode_get_frame(ctx, &frame))
        {
            // printf("decode_get_frame failed\n");
            return;
        }

        if (frame && yuvCallBack)
        {
            if (mpp_frame_get_info_change(frame))
            {
                printf("decode_get_frame get info changed found\n");
                mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
                return;
            }

            MppBuffer buffer = mpp_frame_get_buffer(frame);
            RK_U8 *base = (RK_U8 *)mpp_buffer_get_ptr(buffer);

            // 打印h w
            int width = mpp_frame_get_width(frame);
            int height = mpp_frame_get_height(frame) + 8;
            // printf("h %d, w: %d \n", height, width);

            // NV12 2  BGR888
            bool is_ok = rga_color_cov(RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888, width, height, base, &yuv_buf, yuv_size);
            if (is_ok)
            {
                try
                {
                    cv::Mat bgrImg(height, width, CV_8UC3, yuv_buf);
                    if (!bgrImg.empty())
                    {
                        yuvCallBack(bgrImg.clone());
                    }
                }
                catch (const std::exception &e)
                {
                    printf("创建opencv对象失败!\n");
                    std::cerr << e.what() << '\n';
                    // 释放资源
                    mpp_frame_deinit(&frame);
                    frame = NULL;
                }
            }
            else
            {
                printf("颜色格式转换失败\n");
            }
            // 释放资源
            mpp_frame_deinit(&frame);
            frame = NULL;
        }
        else
        {
            // printf("decode_get_frame get frame failed\n");
        }

        mpp_packet_deinit(&mppPacket);
        mppPacket = NULL;
    }

    void stopDecode()
    {
        if (ctx)
        {
            mpi->reset(ctx);
            mpp_destroy(ctx);
            ctx = NULL;
        }
    }
    void setDecodeCallBack(std::function<void(cv::Mat yuvImg)> oncall) { yuvCallBack = oncall; }
};

#endif