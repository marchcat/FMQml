#include "LinuxAdminPolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace {

LinuxAdminPolicy::Decision allow()
{
    return {true, {}, {}, {}};
}

LinuxAdminPolicy::Decision deny(const QString &code, const QString &message, const QString &path)
{
    return {false, code, message, path};
}

QString normalizedLocalPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool isPseudoFilesystemPath(const QString &path)
{
    static const QStringList deniedPrefixes = {
        QStringLiteral("/proc"),
        QStringLiteral("/sys"),
        QStringLiteral("/dev"),
        QStringLiteral("/run/user")
    };

    for (const QString &prefix : deniedPrefixes) {
        if (path == prefix || path.startsWith(prefix + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

LinuxAdminPolicy::Decision validateLocalPathShape(const QString &path, const QString &role)
{
    if (path.isEmpty()) {
        return deny(QStringLiteral("invalid-path"), QStringLiteral("%1 path is empty").arg(role), path);
    }
    if (path.contains(QChar(0))) {
        return deny(QStringLiteral("invalid-path"), QStringLiteral("%1 path contains an embedded NUL").arg(role), path);
    }
    if (path.contains(QLatin1String("://"))) {
        return deny(QStringLiteral("invalid-path"), QStringLiteral("%1 path must be a local filesystem path").arg(role), path);
    }
    if (path.contains(QLatin1Char('|'))) {
        return deny(QStringLiteral("invalid-path"), QStringLiteral("%1 path must not point inside an archive").arg(role), path);
    }

    const QString normalized = normalizedLocalPath(path);
    if (!QFileInfo(normalized).isAbsolute()) {
        return deny(QStringLiteral("invalid-path"), QStringLiteral("%1 path must be absolute").arg(role), path);
    }
    if (isPseudoFilesystemPath(normalized)) {
        return deny(QStringLiteral("invalid-path"), QStringLiteral("%1 path is inside a pseudo-filesystem").arg(role), normalized);
    }
    return allow();
}

LinuxAdminPolicy::Decision validateRegularSource(const QString &sourcePath)
{
    const LinuxAdminPolicy::Decision shape = validateLocalPathShape(sourcePath, QStringLiteral("Source"));
    if (!shape.allowed) {
        return shape;
    }

    const QFileInfo info(normalizedLocalPath(sourcePath));
    if (info.isSymLink()) {
        return deny(QStringLiteral("symlink-policy-denied"), QStringLiteral("Source symlinks are not supported for administrator operations"), sourcePath);
    }
    if (!info.isFile()) {
        return deny(QStringLiteral("not-found"), QStringLiteral("Source file is missing"), sourcePath);
    }
    return allow();
}

LinuxAdminPolicy::Decision validateDestination(const QString &destinationPath, bool rejectExistingSymlink)
{
    const LinuxAdminPolicy::Decision shape = validateLocalPathShape(destinationPath, QStringLiteral("Destination"));
    if (!shape.allowed) {
        return shape;
    }

    const QString normalized = normalizedLocalPath(destinationPath);
    const QFileInfo destinationInfo(normalized);
    if (rejectExistingSymlink && destinationInfo.isSymLink()) {
        return deny(QStringLiteral("symlink-policy-denied"), QStringLiteral("Destination symlinks are not supported for administrator operations"), normalized);
    }

    const QFileInfo parentInfo(QFileInfo(normalized).absoluteDir().absolutePath());
    if (parentInfo.exists() && parentInfo.isSymLink()) {
        return deny(QStringLiteral("symlink-policy-denied"), QStringLiteral("Destination parent symlinks are not supported for administrator operations"), parentInfo.absoluteFilePath());
    }

    return allow();
}

} // namespace

LinuxAdminPolicy::Decision LinuxAdminPolicy::validate(Operation operation,
                                                      const QString &sourcePath,
                                                      const QString &destinationPath)
{
    switch (operation) {
    case Operation::CopyFile: {
        const Decision source = validateRegularSource(sourcePath);
        if (!source.allowed) {
            return source;
        }
        return validateDestination(destinationPath, true);
    }

    case Operation::AtomicReplace: {
        const Decision source = validateRegularSource(sourcePath);
        if (!source.allowed) {
            return source;
        }
        return validateDestination(destinationPath, true);
    }

    case Operation::MakeDirectory:
        return validateDestination(destinationPath, false);
    }

    return deny(QStringLiteral("invalid-operation"), QStringLiteral("Invalid administrator operation"), {});
}
