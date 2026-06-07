#include "FileProviderFactory.h"

#include "ArchiveFileProvider.h"
#include "ArchiveSupport.h"
#include "FileProviderPluginRegistry.h"
#include "LocalFileProvider.h"

#include <QRegularExpression>

namespace {

bool hasExplicitNonLocalScheme(const QString &path)
{
    const QString trimmed = path.trimmed();
    const int separatorIndex = trimmed.indexOf(QStringLiteral("://"));
    if (separatorIndex <= 0) {
        return false;
    }

    static const QRegularExpression schemePattern(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*$"));
    const QString scheme = trimmed.left(separatorIndex).toLower();
    return schemePattern.match(scheme).hasMatch() && scheme != QStringLiteral("file");
}

} // namespace

std::unique_ptr<FileProvider> FileProviderFactory::createProvider(const QString &path)
{
    if (ArchiveSupport::isArchivePath(path)) {
        return std::make_unique<ArchiveFileProvider>();
    }

    auto &pluginRegistry = FileProviderPluginRegistry::instance();
    if (pluginRegistry.hasProviderForPath(path)) {
        return pluginRegistry.createProvider(path);
    }
    if (hasExplicitNonLocalScheme(path)) {
        return nullptr;
    }

    return std::make_unique<LocalFileProvider>();
}

bool FileProviderFactory::hasPluginProviderForPath(const QString &path)
{
    return FileProviderPluginRegistry::instance().hasProviderForPath(path);
}

QString FileProviderFactory::normalizePath(const QString &path)
{
    if (ArchiveSupport::isArchivePath(path)) {
        return ArchiveSupport::normalizeArchivePath(path);
    }

    const std::unique_ptr<FileProvider> provider = createProvider(path);
    if (provider) {
        return provider->normalizedPath(path);
    }

    return path.trimmed();
}
