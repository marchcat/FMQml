#include "PlacesModel.h"

#include <QDir>
#include <QStandardPaths>
#include <QStorageInfo>

#include "../core/DriveUtils.h"

PlacesModel::PlacesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    refresh();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(5000);
    connect(m_refreshTimer, &QTimer::timeout, this, &PlacesModel::refreshDriveInfo);
    m_refreshTimer->start();
}

int PlacesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant PlacesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const PlaceItem &item = m_items.at(index.row());
    switch (role) {
    case NameRole:         return item.name;
    case PathRole:         return item.path;
    case IconRole:         return item.icon;
    case IsDriveRole:      return item.isDrive;
    case TotalSpaceRole:   return item.totalBytes;
    case FreeSpaceRole:    return item.freeBytes;
    case UsedSpaceRole:    return item.totalBytes - item.freeBytes;
    case UsagePercentRole: return item.totalBytes > 0
                                  ? static_cast<double>(item.totalBytes - item.freeBytes) / static_cast<double>(item.totalBytes)
                                  : 0.0;
    case FileSystemRole:   return item.fileSystem;
    case DriveTypeRole:    return item.driveType;
    case IsReadyRole:      return item.isReady;
    case IsCriticalRole:   return item.isCritical;
    default:               return {};
    }
}

QHash<int, QByteArray> PlacesModel::roleNames() const
{
    return {
        {NameRole,         "name"},
        {PathRole,         "path"},
        {IconRole,         "icon"},
        {IsDriveRole,      "isDrive"},
        {TotalSpaceRole,   "totalSpace"},
        {FreeSpaceRole,    "freeSpace"},
        {UsedSpaceRole,    "usedSpace"},
        {UsagePercentRole, "usagePercent"},
        {FileSystemRole,   "fileSystem"},
        {DriveTypeRole,    "driveType"},
        {IsReadyRole,      "isReady"},
        {IsCriticalRole,   "isCritical"},
    };
}

// Fills storage info fields of a PlaceItem from QStorageInfo.
static void fillStorageInfo(PlaceItem &item, const QStorageInfo &storage)
{
    item.isReady     = storage.isReady();
    item.totalBytes  = storage.bytesTotal();
    item.freeBytes   = storage.bytesFree();
    item.fileSystem  = QString::fromLatin1(storage.fileSystemType());
    item.driveType   = DriveUtils::detectDriveType(storage);
    item.isCritical  = item.totalBytes > 0
                       && (static_cast<double>(item.freeBytes) / static_cast<double>(item.totalBytes)) < 0.10;
}

void PlacesModel::refresh()
{
    beginResetModel();
    m_items.clear();

    // Standard Places
    struct PathInfo {
        QStandardPaths::StandardLocation loc;
        QString name;
        QString icon;
    };

    const QList<PathInfo> standard = {
        {QStandardPaths::HomeLocation,      QStringLiteral("Home"),      QStringLiteral("home")},
        {QStandardPaths::DesktopLocation,   QStringLiteral("Desktop"),   QStringLiteral("desktop")},
        {QStandardPaths::DownloadLocation,  QStringLiteral("Downloads"), QStringLiteral("download")},
        {QStandardPaths::DocumentsLocation, QStringLiteral("Documents"), QStringLiteral("document")},
        {QStandardPaths::PicturesLocation,  QStringLiteral("Pictures"),  QStringLiteral("image")},
        {QStandardPaths::MusicLocation,     QStringLiteral("Music"),     QStringLiteral("music")},
        {QStandardPaths::MoviesLocation,    QStringLiteral("Videos"),    QStringLiteral("video")}
    };

    for (const auto &info : standard) {
        const QString path = QStandardPaths::writableLocation(info.loc);
        if (!path.isEmpty() && QDir(path).exists()) {
            m_items.append({info.name, QDir(path).absolutePath(), info.icon, false});
        }
    }

    // System Drives
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (storage.isValid()) {
            QString name = storage.displayName();
            if (name.isEmpty()) name = storage.rootPath();

            PlaceItem item;
            item.name    = name;
            item.path    = storage.rootPath();
            item.icon    = QStringLiteral("drive");
            item.isDrive = true;
            fillStorageInfo(item, storage);
            m_items.append(item);
        }
    }

    endResetModel();
}

void PlacesModel::refreshDriveInfo()
{
    // Update only storage data for drive items, no full model reset.
    for (int i = 0; i < m_items.size(); ++i) {
        PlaceItem &item = m_items[i];
        if (!item.isDrive) continue;

        const QStorageInfo storage(item.path);
        if (!storage.isValid()) continue;

        const bool wasReady    = item.isReady;
        const qint64 oldFree   = item.freeBytes;
        const bool wasCritical = item.isCritical;

        fillStorageInfo(item, storage);

        // Notify QML if anything changed
        if (item.isReady != wasReady || item.freeBytes != oldFree || item.isCritical != wasCritical) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {
                FreeSpaceRole,
                UsedSpaceRole,
                UsagePercentRole,
                IsReadyRole,
                IsCriticalRole
            });

            // Emit low disk space warning once the drive goes critical
            if (item.isCritical && !wasCritical) {
                emit lowDiskSpaceWarning(item.name, item.freeBytes);
            }
        }
    }
}
