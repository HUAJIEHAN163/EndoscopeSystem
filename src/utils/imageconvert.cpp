#include "utils/imageconvert.h"
#include <opencv2/imgproc.hpp>

QImage ImageConvert::matToQImage(const cv::Mat &mat) {
    if (mat.empty()) return QImage();

    switch (mat.type()) {
    case CV_8UC1: {
        QImage image(mat.data, mat.cols, mat.rows, mat.step,
                     QImage::Format_Grayscale8);
        return image.copy();
    }
    case CV_8UC3: {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data, rgb.cols, rgb.rows, rgb.step,
                     QImage::Format_RGB888);
        return image.copy();
    }
    case CV_8UC4: {
        QImage image(mat.data, mat.cols, mat.rows, mat.step,
                     QImage::Format_ARGB32);
        return image.copy();
    }
    default:
        return QImage();
    }
}

cv::Mat ImageConvert::qimageToMat(const QImage &image) {
    if (image.isNull()) return cv::Mat();

    switch (image.format()) {
    case QImage::Format_RGB888: {
        cv::Mat mat(image.height(), image.width(), CV_8UC3,
                    const_cast<uchar*>(image.constBits()), image.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    }
    case QImage::Format_ARGB32:
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32_Premultiplied: {
        cv::Mat mat(image.height(), image.width(), CV_8UC4,
                    const_cast<uchar*>(image.constBits()), image.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    case QImage::Format_Grayscale8: {
        cv::Mat mat(image.height(), image.width(), CV_8UC1,
                    const_cast<uchar*>(image.constBits()), image.bytesPerLine());
        return mat.clone();
    }
    default: {
        // 转为 RGB888 再处理
        QImage converted = image.convertToFormat(QImage::Format_RGB888);
        return qimageToMat(converted);
    }
    }
}
