#ifndef PRESETMANAGER_H
#define PRESETMANAGER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include "processing/imageprocessor.h"

// 预设管理器：加载/保存/切换预设参数
class PresetManager {
public:
    // 从目录加载所有 .json 预设文件
    static QMap<QString, ImageProcessor::Config> loadAll(const QString &dir);

    // 加载单个预设文件
    static bool load(const QString &path, QString &name, ImageProcessor::Config &cfg);

    // 保存当前参数为预设文件
    static bool save(const QString &path, const QString &name, const ImageProcessor::Config &cfg);
};

#endif // PRESETMANAGER_H
