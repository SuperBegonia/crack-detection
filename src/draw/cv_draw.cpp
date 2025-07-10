#include "cv_draw.h"
#include "utils/logging.h"
#include <iomanip>
#include <map>

void DrawDetections(cv::Mat &img, const std::vector<Detection> &objects, std::chrono::time_point<std::chrono::system_clock> &t, int new_id, int now_id)
{
    // 输出检测到的对象数量
    // std::cout << "draw " << objects.size() << " objects" << std::endl;

    std::map<std::string, int> class_count;

    for (const auto &object : objects)
    {
        if (object.box.width * object.box.height < 200)
        {
            continue;
        }

        // 更新类别计数
        class_count[object.className]++;

        // 绘制目标框，使用更加柔和的颜色和较细的边框
        cv::Scalar boxColor = cv::Scalar(0, 255, 0); // 可以根据需要调整为你喜欢的颜色
        cv::rectangle(img, object.box, boxColor, 3, 8, 0);  // 更细的框和更柔和的颜色
        
        // 给文本添加阴影
        std::ostringstream oss;
        oss << object.className << " " << std::fixed << std::setprecision(1) << object.confidence;
        std::string draw_string = oss.str();

        // 设置文本位置，确保不超出框的边界
        cv::Point text_position(object.box.x, object.box.y - 10);
        if (text_position.y < 0) text_position.y = object.box.y + 20;  // 防止文字超出图像上边界

        // 为文字添加阴影效果
        cv::putText(img, draw_string, text_position + cv::Point(2, 2), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
        cv::putText(img, draw_string, text_position, cv::FONT_HERSHEY_SIMPLEX, 0.8, object.color, 2, cv::LINE_AA);
    }

    // 计算当前时间与传入时间之间的差异
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - t).count();

    // 构造 ID 信息字符串
    std::string id_str = "wait=" + std::to_string(new_id - now_id);
    // 构造时间差信息字符串
    std::string time_diff_str = "Inference Time=" + std::to_string(duration) + " ms" + " obj=" + std::to_string(objects.size());

    // 设置文本位置
    cv::Point textOrg(10, 30); // 左上角起点位置
    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 1;
    int thickness = 2;
    cv::Scalar color = cv::Scalar(0, 0, 255);

    // 绘制 ID 信息
    cv::putText(img, id_str, textOrg, fontFace, fontScale, color, thickness, cv::LINE_AA);

    // 计算下一个文本的 y 坐标位置
    textOrg.y += 30; // 30 是一个估计的行高，具体值可以根据实际字体大小调整

    // 绘制时间差信息
    cv::putText(img, time_diff_str, textOrg, fontFace, fontScale, color, thickness, cv::LINE_AA);

    // 绘制各类别的数量信息
    int y_offset = 30; // 初始Y偏移量
    for (const auto &entry : class_count)
    {
        std::string count_str = entry.first + ": " + std::to_string(entry.second);
        // 确保文本不超出右边界
        cv::putText(img, count_str, cv::Point(img.cols - 200, y_offset), fontFace, 0.8, color, thickness, cv::LINE_AA);
        y_offset += 30;  // 每行文本的间隔
    }

    // 确保文本不会越界，调整Y坐标
    if (y_offset > img.rows - 30)
    {
        y_offset = img.rows - 30;  // 如果超出了图像的下边界，将y坐标设置为图像底部
    }
}

