#ifndef IMAGECONVERT_H
#define IMAGECONVERT_H

#include <QImage>
#include <opencv2/core.hpp>

class ImageConvert {
public:
    // cv::Mat (BGR) → QImage (RGB888), 深拷贝
    static QImage matToQImage(const cv::Mat &mat);

    // QImage → cv::Mat (BGR), 深拷贝
    static cv::Mat qimageToMat(const QImage &image);
};

#endif // IMAGECONVERT_H
