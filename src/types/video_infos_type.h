#pragma once



// video_infos_type.h

// 定义视频解码器信息的结构体
struct video_decoder_info
{
    int width;          // 视频宽度（单位：像素）
    int height;         // 视频高度（单位：像素）
    int fps;            // 帧率（每秒帧数）
    int bitrate;        // 比特率（单位：比特每秒）
    int decoder_delay;  // 解码器延迟（单位：毫秒）
};


struct  video_encoder_info
{
    int width;          // 视频宽度（单位：像素）
    int height;         // 视频高度（单位：像素）
    int fps;            // 帧率（每秒帧数）
    int bitrate;        // 比特率（单位：比特每秒）
    int encoder_delay;  // 编码器延迟（单位：毫秒）
};

