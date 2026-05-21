#include "DriveUtils.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QtGlobal>

namespace DriveUtils {

QString detectDriveType(const QStorageInfo &info)
{
#ifdef Q_OS_WIN
    const QString root = info.rootPath();
    // GetDriveType expects a path like "C:\\"
    const QString native = root.endsWith('/') || root.endsWith('\\')
                               ? root
                               : root + QStringLiteral("\\");
    const std::wstring wpath = native.toStdWString();
    const UINT driveType = ::GetDriveTypeW(wpath.c_str());

    switch (driveType) {
    case DRIVE_REMOVABLE:
        return QStringLiteral("usb");
    case DRIVE_FIXED:
        // No reliable cross-platform SSD detection via WinAPI without extra queries.
        // Default fixed drives to "hdd"; can be refined later with DeviceIoControl.
        return QStringLiteral("hdd");
    case DRIVE_REMOTE:
        return QStringLiteral("network");
    case DRIVE_CDROM:
        return QStringLiteral("optical");
    case DRIVE_RAMDISK:
        return QStringLiteral("hdd");
    default:
        break;
    }
#else
    Q_UNUSED(info)
#endif
    return QStringLiteral("hdd");
}

QString formatSize(qint64 bytes)
{
    if (bytes < 0) {
        return QStringLiteral("—");
    }

    constexpr qint64 KB = 1024LL;
    constexpr qint64 MB = 1024LL * KB;
    constexpr qint64 GB = 1024LL * MB;
    constexpr qint64 TB = 1024LL * GB;

    if (bytes >= TB) {
        double val = static_cast<double>(bytes) / static_cast<double>(TB);
        return QStringLiteral("%1 TB").arg(val, 0, 'f', val < 10.0 ? 2 : (val < 100.0 ? 1 : 0));
    }
    if (bytes >= GB) {
        double val = static_cast<double>(bytes) / static_cast<double>(GB);
        return QStringLiteral("%1 GB").arg(val, 0, 'f', val < 10.0 ? 2 : (val < 100.0 ? 1 : 0));
    }
    if (bytes >= MB) {
        double val = static_cast<double>(bytes) / static_cast<double>(MB);
        return QStringLiteral("%1 MB").arg(val, 0, 'f', val < 10.0 ? 1 : 0);
    }
    if (bytes >= KB) {
        double val = static_cast<double>(bytes) / static_cast<double>(KB);
        return QStringLiteral("%1 KB").arg(val, 0, 'f', 0);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace DriveUtils
