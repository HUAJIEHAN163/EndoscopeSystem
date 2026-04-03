#include "ui/presetmanager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

QMap<QString, ImageProcessor::Config> PresetManager::loadAll(const QString &dir) {
    QMap<QString, ImageProcessor::Config> presets;
    QDir d(dir);
    for (const QString &file : d.entryList({"*.json"}, QDir::Files)) {
        QString name;
        ImageProcessor::Config cfg;
        if (load(d.filePath(file), name, cfg))
            presets[name] = cfg;
    }
    return presets;
}

bool PresetManager::load(const QString &path, QString &name, ImageProcessor::Config &cfg) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    name = obj["name"].toString();
    cfg.whiteBalance   = obj["whiteBalance"].toBool();
    cfg.clahe          = obj["clahe"].toBool();
    cfg.claheClipLimit = obj["claheClipLimit"].toDouble(3.0);
    cfg.undistort      = obj["undistort"].toBool();
    cfg.dehaze         = obj["dehaze"].toBool();
    cfg.dehazeOmega    = obj["dehazeOmega"].toDouble(0.95);
    cfg.dehazeRadius   = obj["dehazeRadius"].toInt(7);
    cfg.sharpen        = obj["sharpen"].toBool();
    cfg.sharpenAmount  = obj["sharpenAmount"].toDouble(1.5);
    cfg.denoise        = obj["denoise"].toBool();
    cfg.denoiseD       = obj["denoiseD"].toInt(5);
    cfg.edgeDetect     = obj["edgeDetect"].toBool();
    cfg.threshold      = obj["threshold"].toBool();
    cfg.thresholdValue = obj["thresholdValue"].toInt(128);
    return true;
}

bool PresetManager::save(const QString &path, const QString &name, const ImageProcessor::Config &cfg) {
    QJsonObject obj;
    obj["name"]           = name;
    obj["whiteBalance"]   = cfg.whiteBalance;
    obj["clahe"]          = cfg.clahe;
    obj["claheClipLimit"] = cfg.claheClipLimit;
    obj["undistort"]      = cfg.undistort;
    obj["dehaze"]         = cfg.dehaze;
    obj["dehazeOmega"]    = cfg.dehazeOmega;
    obj["dehazeRadius"]   = cfg.dehazeRadius;
    obj["sharpen"]        = cfg.sharpen;
    obj["sharpenAmount"]  = cfg.sharpenAmount;
    obj["denoise"]        = cfg.denoise;
    obj["denoiseD"]       = cfg.denoiseD;
    obj["edgeDetect"]     = cfg.edgeDetect;
    obj["threshold"]      = cfg.threshold;
    obj["thresholdValue"] = cfg.thresholdValue;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}
