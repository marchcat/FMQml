#include "LocalFileProvider.h"

#include "DriveUtils.h"
#include "ArchiveSupport.h"
#include "CleanupSubsystem.h"
#include "LocalFileBadgeResolver.h"
#include "LocalMountPointIndex.h"
#include "LinuxAdminBroker.h"
#ifdef Q_OS_LINUX
#include "LinuxFileEnumerator.h"
#endif

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QLocale>
#include <QStringList>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
enum class FileMutationKind {
    DeleteFile,
    DeleteFolder,
    Move,
    Rename,
    CreateFolder,
    CreateFile,
    Read,
    Write
};

#ifdef Q_OS_LINUX
class AdminStagedReadFile final : public QFile
{
public:
    AdminStagedReadFile(const QString &path, const QString &leaseId)
        : QFile(path)
        , m_leaseId(leaseId)
    {
    }

    ~AdminStagedReadFile() override
    {
        close();
        CleanupSubsystem::instance().scheduleDelete(m_leaseId);
    }

private:
    QString m_leaseId;
};
#endif

bool isImageSuffix(const QString &suffix)
{
    static const QStringList imageSuffixes = {
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("png"),
        QStringLiteral("gif"),
        QStringLiteral("bmp"),
        QStringLiteral("webp"),
        QStringLiteral("ico"),
        QStringLiteral("svg"),
        QStringLiteral("svgz"),
        QStringLiteral("tif"),
        QStringLiteral("tiff"),
        QStringLiteral("avif"),
        QStringLiteral("heic")
    };
    return imageSuffixes.contains(suffix.toLower());
}

bool hasThumbnailSuffix(const QString &suffix)
{
    static const QStringList thumbnailSuffixes = {
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("gif"),  QStringLiteral("bmp"),  QStringLiteral("webp"),
        QStringLiteral("ico"),
        QStringLiteral("svg"),  QStringLiteral("svgz"),
        QStringLiteral("mp3"),  QStringLiteral("flac"), QStringLiteral("ogg"),
        QStringLiteral("m4a"),  QStringLiteral("m4b"),  QStringLiteral("wav"),
        QStringLiteral("wma"),
        QStringLiteral("mp4"),  QStringLiteral("avi"),  QStringLiteral("mkv"),
        QStringLiteral("mov"),  QStringLiteral("wmv"),
        QStringLiteral("pdf"),
        QStringLiteral("ttf"),  QStringLiteral("otf"),  QStringLiteral("woff"),
        QStringLiteral("woff2")
    };
    return thumbnailSuffixes.contains(suffix.toLower());
}

#ifdef Q_OS_WIN
QString suffixFromName(const QString &name)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1) {
        return {};
    }
    return name.mid(dot + 1);
}
#endif


#ifdef Q_OS_WIN
DWORD entryAttributesWindows(const QFileInfo &fileInfo)
{
    const QString nativePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    return GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativePath.utf16()));
}
#endif

bool sameFilesystemPath(const QString &left, const QString &right)
{
    const QString normalizedLeft = QDir::cleanPath(QDir::fromNativeSeparators(left));
    const QString normalizedRight = QDir::cleanPath(QDir::fromNativeSeparators(right));
#ifdef Q_OS_WIN
    return normalizedLeft.compare(normalizedRight, Qt::CaseInsensitive) == 0;
#else
    return normalizedLeft == normalizedRight;
#endif
}

QString normalizedFilesystemPath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }

    const QString value = QDir::fromNativeSeparators(path);
    QString absolutePath = QDir::isAbsolutePath(value)
        ? value
        : QDir::current().absoluteFilePath(value);
    return QDir::cleanPath(absolutePath);
}

bool localProviderNavTraceEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("FM_NAV_TRACE");
    return enabled;
}

void traceLocalProviderNav(const char *stage, const QString &path = {}, const QString &detail = {})
{
    if (!localProviderNavTraceEnabled()) {
        return;
    }

    qInfo().noquote() << "[FM_NAV][local-provider]" << stage
                      << "path=" << QDir::toNativeSeparators(path)
                      << detail;
}

void traceLocalProviderSlow(const char *stage, const QString &path, qint64 elapsedMs, const QString &detail = {})
{
    if (!localProviderNavTraceEnabled() || elapsedMs < 5) {
        return;
    }

    traceLocalProviderNav(stage, path,
                          QStringLiteral("elapsedMs=%1 %2").arg(elapsedMs).arg(detail));
}

FileEntry entryFromInfo(const QFileInfo &fileInfo)
{
    FileEntry entry;
    entry.name = fileInfo.fileName();
    entry.path = fileInfo.absoluteFilePath();
    entry.suffix = fileInfo.suffix();
    entry.size = fileInfo.size();
    entry.modified = fileInfo.lastModified();
    entry.created = fileInfo.birthTime().isValid() ? fileInfo.birthTime() : fileInfo.lastModified();
#ifdef Q_OS_WIN
    const DWORD attributes = entryAttributesWindows(fileInfo);
    const bool hasNativeAttributes = attributes != INVALID_FILE_ATTRIBUTES;
    const bool isDirectory = hasNativeAttributes
        ? ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        : fileInfo.isDir();
    const bool isHidden = hasNativeAttributes
        ? ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0 || entry.name.startsWith(QLatin1Char('.')))
        : (fileInfo.isHidden() || entry.name.startsWith(QLatin1Char('.')));
    const bool isReadOnly = hasNativeAttributes
        ? ((attributes & FILE_ATTRIBUTE_READONLY) != 0)
        : !fileInfo.isWritable();
    const bool isSystem = hasNativeAttributes
        ? ((attributes & FILE_ATTRIBUTE_SYSTEM) != 0)
        : false;
    const bool isLink = hasNativeAttributes
        ? ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        : fileInfo.isSymLink();
    entry.isDirectory = isDirectory;
    entry.isHidden = isHidden;
    entry.isReadOnly = isReadOnly;
    entry.isSystem = isSystem;
#else
    entry.isDirectory = fileInfo.isDir();
    entry.isHidden = fileInfo.isHidden() || fileInfo.fileName().startsWith(QLatin1Char('.'));
    entry.isReadOnly = !fileInfo.isWritable();
    const bool isLink = fileInfo.isSymLink();
#endif

    QLocale loc;
    entry.sizeText = entry.isDirectory
        ? QString()
        : DriveUtils::formatSize(entry.size);
    entry.modifiedText = loc.toString(entry.modified, QLocale::ShortFormat);
    entry.createdText  = loc.toString(entry.created,  QLocale::ShortFormat);

    // Build attributes string
    QString attrs;
    if (entry.isDirectory) attrs += QLatin1Char('D');
    if (entry.isHidden)    attrs += QLatin1Char('H');
    if (entry.isReadOnly)  attrs += QLatin1Char('R');
    if (entry.isSystem)    attrs += QLatin1Char('S');
    if (isLink)            attrs += QLatin1Char('L');
    entry.attributesText = attrs;

    const LocalFileBadgeState badgeState = LocalFileBadgeResolver::resolve(fileInfo, isLink);
    entry.isSymLink = badgeState.isSymLink;
    entry.isBrokenSymLink = badgeState.isBrokenSymLink;
    entry.isLocked = badgeState.isLocked;
    entry.primaryBadgeKind = badgeState.primaryBadgeKind;

    entry.isImage = !entry.isDirectory && isImageSuffix(entry.suffix);
    entry.hasThumbnail = !entry.isDirectory && hasThumbnailSuffix(entry.suffix);
    return entry;
}

#ifdef Q_OS_LINUX
QList<FileEntry> entriesFromAdminHelper(const QJsonArray &jsonEntries, const QLocale &locale)
{
    QList<FileEntry> entries;
    entries.reserve(jsonEntries.size());
    for (const QJsonValue &value : jsonEntries) {
        const QJsonObject source = value.toObject();
        FileEntry entry;
        entry.name = source.value(QStringLiteral("name")).toString();
        entry.path = source.value(QStringLiteral("path")).toString();
        entry.suffix = source.value(QStringLiteral("suffix")).toString();
        entry.size = source.value(QStringLiteral("size")).toVariant().toLongLong();
        entry.modified = QDateTime::fromMSecsSinceEpoch(source.value(QStringLiteral("modifiedMs")).toVariant().toLongLong());
        entry.created = QDateTime::fromMSecsSinceEpoch(source.value(QStringLiteral("createdMs")).toVariant().toLongLong());
        entry.isDirectory = source.value(QStringLiteral("isDirectory")).toBool(false);
        entry.isHidden = source.value(QStringLiteral("isHidden")).toBool(false);
        entry.isReadOnly = source.value(QStringLiteral("isReadOnly")).toBool(false);
        entry.isSymLink = source.value(QStringLiteral("isSymLink")).toBool(false);
        entry.isBrokenSymLink = source.value(QStringLiteral("isBrokenSymLink")).toBool(false);
        entry.isLocked = source.value(QStringLiteral("isLocked")).toBool(false);
        entry.isMountPoint = entry.isDirectory && LocalMountPointIndex::isMountPoint(entry.path);
        entry.attributesText = source.value(QStringLiteral("attributesText")).toString();
        entry.primaryBadgeKind = LocalFileBadgeResolver::primaryBadgeKind(
            entry.isBrokenSymLink,
            entry.isSymLink,
            entry.isMountPoint,
            entry.isLocked,
            !entry.isDirectory && ArchiveSupport::isArchiveExtension(entry.suffix));
        entry.isImage = !entry.isDirectory && isImageSuffix(entry.suffix);
        entry.hasThumbnail = !entry.isDirectory && hasThumbnailSuffix(entry.suffix);
        entry.sizeText = entry.isDirectory ? DriveUtils::formatSize(0) : DriveUtils::formatSize(entry.size);
        entry.modifiedText = locale.toString(entry.modified, QLocale::ShortFormat);
        entry.createdText = locale.toString(entry.created, QLocale::ShortFormat);
        entries.append(entry);
    }
    return entries;
}
#endif

#ifdef Q_OS_WIN
QString localExtendedLengthWindowsPath(const QString &path)
{
    QString nativePath = QDir::toNativeSeparators(path);
    if (nativePath.startsWith(QStringLiteral("\\\\?\\"))) {
        return nativePath;
    }
    if (nativePath.startsWith(QStringLiteral("\\\\"))) {
        return QStringLiteral("\\\\?\\UNC\\") + nativePath.mid(2);
    }
    return QStringLiteral("\\\\?\\") + nativePath;
}

QString windowsSearchPattern(const QString &path)
{
    QString pattern = localExtendedLengthWindowsPath(path);
    if (!pattern.endsWith(QLatin1Char('\\'))) {
        pattern += QLatin1Char('\\');
    }
    pattern += QLatin1Char('*');
    return pattern;
}

QDateTime dateTimeFromFileTime(const FILETIME &fileTime)
{
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    if (value.QuadPart == 0) {
        return {};
    }

    constexpr quint64 windowsEpochToUnixEpoch100ns = Q_UINT64_C(116444736000000000);
    if (value.QuadPart < windowsEpochToUnixEpoch100ns) {
        return {};
    }
    const qint64 msecs = static_cast<qint64>((value.QuadPart - windowsEpochToUnixEpoch100ns) / 10000);
    return QDateTime::fromMSecsSinceEpoch(msecs);
}

QString suffixFromFileName(const QString &name)
{
    return suffixFromName(name);
}

qint64 sizeFromFindData(const WIN32_FIND_DATAW &findData)
{
    ULARGE_INTEGER value{};
    value.LowPart = findData.nFileSizeLow;
    value.HighPart = findData.nFileSizeHigh;
    return static_cast<qint64>(value.QuadPart);
}

FileEntry entryFromFindData(const WIN32_FIND_DATAW &findData, const QString &parentPrefix, const QLocale &loc)
{
    const DWORD attributes = findData.dwFileAttributes;
    const QString name = QString::fromWCharArray(findData.cFileName);
    const bool isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool isHidden = (attributes & FILE_ATTRIBUTE_HIDDEN) != 0 || name.startsWith(QLatin1Char('.'));
    const bool isReadOnly = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    const bool isLink = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
        && (findData.dwReserved0 == IO_REPARSE_TAG_SYMLINK
            || findData.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);

    FileEntry entry;
    entry.name = name;
    entry.path = parentPrefix + name;
    entry.suffix = suffixFromFileName(name);
    entry.size = isDirectory ? 0 : sizeFromFindData(findData);
    entry.modified = dateTimeFromFileTime(findData.ftLastWriteTime);
    entry.created = dateTimeFromFileTime(findData.ftCreationTime);
    if (!entry.created.isValid()) {
        entry.created = entry.modified;
    }
    entry.isDirectory = isDirectory;
    entry.isHidden = isHidden;
    entry.isReadOnly = isReadOnly;
    entry.isSystem = (attributes & FILE_ATTRIBUTE_SYSTEM) != 0;

    entry.sizeText = entry.isDirectory
        ? QString()
        : DriveUtils::formatSize(entry.size);
    entry.modifiedText = loc.toString(entry.modified, QLocale::ShortFormat);
    entry.createdText = loc.toString(entry.created, QLocale::ShortFormat);

    QString attrs;
    if (entry.isDirectory) attrs += QLatin1Char('D');
    if (entry.isHidden)    attrs += QLatin1Char('H');
    if (entry.isReadOnly)  attrs += QLatin1Char('R');
    if (entry.isSystem)    attrs += QLatin1Char('S');
    if (isLink)            attrs += QLatin1Char('L');
    entry.attributesText = attrs;

    const QFileInfo fileInfo(entry.path);
    const LocalFileBadgeState badgeState = LocalFileBadgeResolver::resolve(fileInfo, isLink);
    entry.isSymLink = badgeState.isSymLink;
    entry.isBrokenSymLink = badgeState.isBrokenSymLink;
    entry.isLocked = badgeState.isLocked;
    entry.primaryBadgeKind = badgeState.primaryBadgeKind;

    entry.isImage = !entry.isDirectory && isImageSuffix(entry.suffix);
    entry.hasThumbnail = !entry.isDirectory && hasThumbnailSuffix(entry.suffix);
    return entry;
}

HANDLE findFirstFileBasic(const QString &pattern, WIN32_FIND_DATAW *findData)
{
    HANDLE handle = FindFirstFileExW(reinterpret_cast<LPCWSTR>(pattern.utf16()),
                                     FindExInfoBasic,
                                     findData,
                                     FindExSearchNameMatch,
                                     nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER) {
        handle = FindFirstFileExW(reinterpret_cast<LPCWSTR>(pattern.utf16()),
                                  FindExInfoBasic,
                                  findData,
                                  FindExSearchNameMatch,
                                  nullptr,
                                  0);
    }
    return handle;
}

QString verbForMutation(FileMutationKind kind)
{
    switch (kind) {
    case FileMutationKind::DeleteFile:
    case FileMutationKind::DeleteFolder:
        return QStringLiteral("delete");
    case FileMutationKind::Move:
        return QStringLiteral("move");
    case FileMutationKind::Rename:
        return QStringLiteral("rename");
    case FileMutationKind::CreateFolder:
        return QStringLiteral("create folder in");
    case FileMutationKind::CreateFile:
        return QStringLiteral("create file in");
    case FileMutationKind::Read:
        return QStringLiteral("read");
    case FileMutationKind::Write:
        return QStringLiteral("write");
    }
    return QStringLiteral("modify");
}

QString targetForMutation(FileMutationKind kind, const QString &path)
{
    switch (kind) {
    case FileMutationKind::CreateFolder:
    case FileMutationKind::CreateFile:
        return QDir::toNativeSeparators(QFileInfo(path).absolutePath());
    default:
        return QDir::toNativeSeparators(path);
    }
}

QString windowsMutationErrorMessage(FileMutationKind kind, const QString &path, DWORD errorCode)
{
    const QString target = targetForMutation(kind, path);
    const QString verb = verbForMutation(kind);

    switch (errorCode) {
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return QStringLiteral("Access denied: cannot %1 %2").arg(verb, target);
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return QStringLiteral("Cannot %1 %2: the item is being used by another process").arg(verb, target);
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return QStringLiteral("Cannot %1 %2: the item no longer exists").arg(verb, target);
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return QStringLiteral("Cannot %1 %2: an item with the same name already exists").arg(verb, target);
    case ERROR_DIR_NOT_EMPTY:
        return QStringLiteral("Cannot %1 %2: the folder is not empty").arg(verb, target);
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return QStringLiteral("Cannot %1 %2: the drive is full").arg(verb, target);
    case ERROR_WRITE_PROTECT:
        return QStringLiteral("Cannot %1 %2: the destination is write-protected").arg(verb, target);
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_INVALID_NAME:
        return QStringLiteral("Cannot %1 %2: the name is invalid").arg(verb, target);
    default:
        break;
    }

    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
        | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags,
                                        nullptr,
                                        errorCode,
                                        0,
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    QString detail;
    if (length > 0 && buffer) {
        detail = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
        LocalFree(buffer);
    }

    if (detail.isEmpty()) {
        return QStringLiteral("Cannot %1 %2").arg(verb, target);
    }
    return QStringLiteral("Cannot %1 %2: %3").arg(verb, target, detail);
}

DWORD fileAttributesWindows(const QString &path)
{
    return GetFileAttributesW(reinterpret_cast<LPCWSTR>(localExtendedLengthWindowsPath(path).utf16()));
}
#endif

#ifdef Q_OS_LINUX
QString posixErrorMessage(FileMutationKind kind, const QString &path, int errorCode)
{
    Q_UNUSED(kind)
    return QStringLiteral("Cannot read %1: %2")
        .arg(QDir::toNativeSeparators(path), QString::fromLocal8Bit(std::strerror(errorCode)));
}
#endif
}

LocalFileProvider::LocalFileProvider(QObject *parent)
    : FileProvider(parent)
{
}

LocalFileProvider::~LocalFileProvider()
{
    cancel();
    for (QFuture<void> &future : m_scanFutures) {
        future.waitForFinished();
    }
}
