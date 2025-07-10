

#ifndef RK3588_DEMO_CV_DRAW_H
#define RK3588_DEMO_CV_DRAW_H

#include <opencv2/opencv.hpp>

#include "types/yolo_datatype.h"
#include "utils/rk_helper.cpp"


void DrawDetections(cv::Mat& img, const std::vector<Detection>& objects,std::chrono::time_point<std::chrono::system_clock>& t,int new_id,int now_id);





#endif //RK3588_DEMO_CV_DRAW_H
