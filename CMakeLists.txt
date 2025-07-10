# 设置最低版本号
cmake_minimum_required(VERSION 3.11 FATAL_ERROR)
# 设置项目名称
project(rk3588-demo VERSION 0.0.1 LANGUAGES CXX)

# 输出系统信息
message(STATUS "System: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")



# 设置编译器
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置库架构
set(LIB_ARCH "aarch64")
set(DEVICE_NAME "RK3588")

#  rknn_api 文件夹路径
set(RKNN_API_PATH ${CMAKE_CURRENT_SOURCE_DIR}/librknn_api)
#  rknn_api include 路径
set(RKNN_API_INCLUDE_PATH ${RKNN_API_PATH}/include)
#  rknn_api lib 路径
set(RKNN_API_LIB_PATH ${RKNN_API_PATH}/${LIB_ARCH}/librknnrt.so)

# 寻找OpenCV库，使用自定义的OpenCV_DIR
set(3RDPARTY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
set(RGA_DIR ${3RDPARTY_PATH}/rga/${DEVICE_NAME})
set(RGA_LIB ${RGA_DIR}/lib/Linux/${LIB_ARCH}/librga.a)

find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})

find_package(OpenSSL REQUIRED)

# 添加rockchip  ffmpeg 目录
set(FFMPEG_ROCKCHIP_DIR ${3RDPARTY_PATH}/ffmpeg-rockchip)
set(FFMPEG_ROCKCHIP_LIB ${FFMPEG_ROCKCHIP_DIR}/lib/)
include_directories(${FFMPEG_ROCKCHIP_DIR}/include)
link_directories(${FFMPEG_ROCKCHIP_LIB})




#set(OpenCV_DIR ${3RDPARTY_PATH}/opencv/opencv-linux-${LIB_ARCH}/share/OpenCV)

find_package(Threads REQUIRED)

find_package(OpenCV REQUIRED)
# 输出OpenCV信息
message(STATUS "opencv include path: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "opencv lib path: ${OpenCV_LIBS}")
# 打印每个库的完整路径
foreach(lib ${OpenCV_LIBS})
    find_library(lib_path NAMES ${lib} HINTS ${OpenCV_LIB_DIR})
    if(lib_path)
        message(STATUS "Found OpenCV library: ${lib_path}")
    else()
        message(STATUS "Could not find path for OpenCV library: ${lib}")
    endif()
endforeach()

# 用来搜索头文件的目录
include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${RKNN_API_INCLUDE_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${RGA_DIR}/include
)

# 构建预处理和后处理库
add_library(nn_process STATIC
            src/process/preprocess.cpp
            src/process/postprocess.cpp
)
# 链接库
target_link_libraries(nn_process
    ${OpenCV_LIBS}
    ${RGA_LIB}
)

# 构建自定义封装API库
add_library(rknn_engine STATIC src/engine/rknn_engine.cpp) 
# 链接库
target_link_libraries(rknn_engine
    ${RKNN_API_LIB_PATH}
)
# yolov8_lib
add_library(yolov8_detection_lib STATIC src/yolo/Yolov8Detection.cpp)
# 链接库
target_link_libraries(yolov8_detection_lib
    rknn_engine
    nn_process
)

# draw_lib
add_library(draw_lib STATIC src/draw/cv_draw.cpp)
# 链接库
target_link_libraries(draw_lib
    ${OpenCV_LIBS}
)



add_library(rk_helper SHARED src/utils/rk_helper.cpp)
# 链接库
target_link_libraries(rk_helper
    rockchip_mpp
    ${RGA_LIB}
)


add_executable(Ai 
    src/App.cpp
    src/yolo/yolov8_thread_pool.cpp
    src/utils/rk_helper.cpp
)


target_link_libraries(Ai

   ${FFMPEG_ROCKCHIP_LIB}/libavformat.a
    ${FFMPEG_ROCKCHIP_LIB}/libavcodec.a
    ${FFMPEG_ROCKCHIP_LIB}/libavutil.a
    ${FFMPEG_ROCKCHIP_LIB}/libswresample.a
    ${FFMPEG_ROCKCHIP_LIB}/libswscale.a

    -pthread
    -lva
    -lva-drm
    -lva-x11
    -lX11
    -lvdpau
    drm
    libnats_static.a
    rockchip_mpp
    ${OpenCV_LIBS}
    z
    ${RGA_LIB}
    draw_lib
    protobuf-c.a
    yolov8_detection_lib
    Threads::Threads  # 添加线程库
    rk_helper
    OpenSSL::SSL OpenSSL::Crypto
    png
    
)

add_executable(v4l2_h264
    src/v4l2_h264.cpp
)

target_link_libraries(v4l2_h264
    ${FFMPEG_ROCKCHIP_LIB}/libavformat.a
    ${FFMPEG_ROCKCHIP_LIB}/libavcodec.a
    ${FFMPEG_ROCKCHIP_LIB}/libavutil.a
    ${FFMPEG_ROCKCHIP_LIB}/libswresample.a
    ${FFMPEG_ROCKCHIP_LIB}/libswscale.a
    -pthread
    -lva
    -lva-drm
    -lva-x11
    -lX11
    -lvdpau
    drm
    libnats_static.a
    rockchip_mpp
    ${OpenCV_LIBS}
    z
    ${RGA_LIB}
    Threads::Threads
    OpenSSL::SSL OpenSSL::Crypto
)