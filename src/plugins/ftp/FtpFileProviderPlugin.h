#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>

#include "FileProviderPlugin.h"

class FtpFileProviderPlugin final : public QObject, public FileProviderPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID FM_FILE_PROVIDER_PLUGIN_IID)
    Q_INTERFACES(FileProviderPlugin)

public:
    int apiVersion() const override;
    QString pluginId() const override;
    QString displayName() const override;
    QStringList schemes() const override;
    bool canHandle(const QString &path) const override;
    std::unique_ptr<FileProvider> createProvider() override;
};
