#pragma once

#include <functional>
#include <memory>
#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QTemporaryFile>

#include "FileProvider.h"

class MegaClientInterface;
class QTemporaryFile;

namespace MegaProviderRuntime {
inline constexpr qint64 MegaOpenReadFallbackLimitBytes = 512ll * 1024ll * 1024ll;
inline constexpr int MegaSingleDownloadTimeoutMs = 45000;
MegaClientInterface &megaClient();
void setMegaClientForTesting(MegaClientInterface *client);
bool megaProviderTimingEnabled();
bool megaDownloadItemTimingEnabled();
bool megaProviderUploadItemTimingEnabled();
bool megaThumbnailTraceEnabled();
bool megaThumbnailInCooldown();
void startMegaThumbnailCooldown();
QByteArray repairedMegaThumbnailBytes(const QString &normalized);
QString repairedMegaThumbnailIdentityToken(const QString &normalized);
void rememberRepairedMegaThumbnail(const QString &normalized, const QString &thumbnailPath);
bool isMegaThumbnailRepairCandidate(const FileEntry &entry);
bool writeMegaRepairThumbnailFile(const QString &sourceImagePath, const QString &thumbnailPath, QString *error);
int megaUploadConcurrency();
int megaDownloadConcurrency();
bool megaDownloadQuotaError(const QString &errorString);
QVariantList megaAccountStatusProperties();
QVariantMap megaStorageInfoMap();
QVariantMap runBlockingMegaAuthorization(const std::function<int()> &startAuthorization, const QString &successMessage, const QString &startFailureMessage);
class CleanupManagedTemporaryFile final : public QTemporaryFile
{
public:
    explicit CleanupManagedTemporaryFile(const QString &fileTemplate);
    ~CleanupManagedTemporaryFile() override;
    void setCleanupLeaseId(const QString &leaseId);
    QString cleanupLeaseId() const;

private:
    Q_DISABLE_COPY_MOVE(CleanupManagedTemporaryFile)
    QString m_cleanupLeaseId;
};
QString megaOpenReadStagingRoot(const QString &stagingParentPath, const QString &sourcePath);
bool waitForMegaMutation(const std::function<qint64()> &startMutation, const QString &operation, const QString &path, QString *resultPath, QString *errorStr);
}
