#include "MegaProviderRuntime.h"
#include "MegaPath.h"
#include "MegaCache.h"
#include "MegaClient.h"
#include "MegaClientInterface.h"
#include "MegaAuth.h"
#include "MegaPresentation.h"
#include "CleanupSubsystem.h"

#include <megaapi.h>
using namespace mega;

#include <QMutex>
#include <QMutexLocker>
#include <QDebug>
#include <QDateTime>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QImageWriter>
#include <QLocale>
#include <QTimer>
#include <QVariantList>
#include <QWaitCondition>
#include <algorithm>
#include <functional>

namespace MegaProviderRuntime {

constexpr QLatin1StringView MegaSignOutAction{"signOut"};
constexpr QLatin1StringView MegaSignInAction{"signIn"};
constexpr QLatin1StringView MegaAuthStatusAction{"authStatus"};
constexpr QLatin1StringView MegaRepairThumbnailAction{"repairThumbnail"};
constexpr int DefaultMegaDownloadConcurrency = 4;
constexpr int MaxMegaDownloadConcurrency = 4;
constexpr int DefaultMegaUploadConcurrency = 4;
constexpr int MaxMegaUploadConcurrency = 4;
constexpr int MegaThumbnailCooldownMs = 60000;
constexpr int MegaRepairThumbnailMaxSide = 512;
constexpr int MegaRepairThumbnailJpegQuality = 82;
constexpr int MegaRepairedThumbnailCacheTtlMs = 5 * 60 * 1000;
#ifdef FM_MEGA_PROVIDER_TESTING
MegaClientInterface *s_clientForTesting = nullptr;
#endif

void setMegaClientForTesting(MegaClientInterface *client)
{
#ifdef FM_MEGA_PROVIDER_TESTING
    s_clientForTesting = client;
#else
    Q_UNUSED(client)
#endif
}

MegaClientInterface &megaClient()
{
#ifdef FM_MEGA_PROVIDER_TESTING
    if (s_clientForTesting) {
        return *s_clientForTesting;
    }
#endif
    return defaultMegaClient();
}

bool megaProviderTimingEnabled()
{
    return qEnvironmentVariableIsSet("FM_MEGA_TIMING");
}

bool megaDownloadItemTimingEnabled()
{
    return qEnvironmentVariableIsSet("FM_MEGA_DOWNLOAD_ITEM_TIMING");
}

bool megaProviderUploadItemTimingEnabled()
{
    return qEnvironmentVariableIsSet("FM_MEGA_UPLOAD_ITEM_TIMING");
}

bool megaThumbnailTraceEnabled()
{
    return qEnvironmentVariableIsSet("FM_PROVIDER_THUMBNAIL_TRACE");
}

QMutex &megaThumbnailCooldownMutex()
{
    static QMutex mutex;
    return mutex;
}

qint64 &megaThumbnailCooldownUntilMs()
{
    static qint64 until = 0;
    return until;
}

bool megaThumbnailInCooldown()
{
    QMutexLocker locker(&megaThumbnailCooldownMutex());
    return QDateTime::currentMSecsSinceEpoch() < megaThumbnailCooldownUntilMs();
}

void startMegaThumbnailCooldown()
{
    QMutexLocker locker(&megaThumbnailCooldownMutex());
    megaThumbnailCooldownUntilMs() = QDateTime::currentMSecsSinceEpoch() + MegaThumbnailCooldownMs;
}

struct MegaRepairedThumbnailCacheEntry
{
    QByteArray bytes;
    qint64 expiresAtMs = 0;
};

QMutex &megaRepairedThumbnailCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, MegaRepairedThumbnailCacheEntry> &megaRepairedThumbnailCache()
{
    static QHash<QString, MegaRepairedThumbnailCacheEntry> cache;
    return cache;
}

QByteArray repairedMegaThumbnailBytes(const QString &normalized)
{
    QMutexLocker locker(&megaRepairedThumbnailCacheMutex());
    auto &cache = megaRepairedThumbnailCache();
    auto it = cache.find(normalized);
    if (it == cache.end()) {
        if (megaThumbnailTraceEnabled()) {
            qInfo().noquote() << "[MegaThumbnail] repaired-cache-miss" << "path=" << normalized;
        }
        return {};
    }
    if (QDateTime::currentMSecsSinceEpoch() >= it->expiresAtMs) {
        if (megaThumbnailTraceEnabled()) {
            qInfo().noquote() << "[MegaThumbnail] repaired-cache-expired" << "path=" << normalized;
        }
        cache.erase(it);
        return {};
    }
    if (megaThumbnailTraceEnabled()) {
        qInfo().noquote()
            << "[MegaThumbnail] repaired-cache-hit"
            << "path=" << normalized
            << "bytes=" << it->bytes.size();
    }
    return it->bytes;
}

QString repairedMegaThumbnailIdentityToken(const QString &normalized)
{
    QMutexLocker locker(&megaRepairedThumbnailCacheMutex());
    auto &cache = megaRepairedThumbnailCache();
    auto it = cache.find(normalized);
    if (it == cache.end()) {
        return {};
    }
    if (QDateTime::currentMSecsSinceEpoch() >= it->expiresAtMs) {
        cache.erase(it);
        return {};
    }
    const QByteArray digest = QCryptographicHash::hash(it->bytes, QCryptographicHash::Sha1)
                                  .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("repaired:%1:%2")
        .arg(QString::fromLatin1(digest), QString::number(it->bytes.size()));
}

void rememberRepairedMegaThumbnail(const QString &normalized, const QString &thumbnailPath)
{
    QFile file(thumbnailPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (megaThumbnailTraceEnabled()) {
            qInfo().noquote()
                << "[MegaThumbnail] repaired-cache-store-failed"
                << "path=" << normalized
                << "reason=open"
                << "file=" << thumbnailPath;
        }
        return;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        if (megaThumbnailTraceEnabled()) {
            qInfo().noquote()
                << "[MegaThumbnail] repaired-cache-store-failed"
                << "path=" << normalized
                << "reason=empty"
                << "file=" << thumbnailPath;
        }
        return;
    }

    if (megaThumbnailTraceEnabled()) {
        qInfo().noquote()
            << "[MegaThumbnail] repaired-cache-store"
            << "path=" << normalized
            << "bytes=" << bytes.size();
    }
    QMutexLocker locker(&megaRepairedThumbnailCacheMutex());
    megaRepairedThumbnailCache().insert(normalized, MegaRepairedThumbnailCacheEntry{
        bytes,
        QDateTime::currentMSecsSinceEpoch() + MegaRepairedThumbnailCacheTtlMs,
    });
}

bool isMegaThumbnailRepairCandidate(const FileEntry &entry)
{
    if (entry.isDirectory) {
        return false;
    }
    const QString suffix = entry.suffix.toLower();
    return suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("png")
        || suffix == QStringLiteral("webp")
        || suffix == QStringLiteral("bmp")
        || suffix == QStringLiteral("tif")
        || suffix == QStringLiteral("tiff")
        || suffix == QStringLiteral("heic")
        || suffix == QStringLiteral("heif");
}

bool writeMegaRepairThumbnailFile(const QString &sourceImagePath,
                                  const QString &thumbnailPath,
                                  QString *error)
{
    QImageReader reader(sourceImagePath);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        QSize scaledSize = sourceSize;
        scaledSize.scale(QSize(MegaRepairThumbnailMaxSide, MegaRepairThumbnailMaxSide), Qt::KeepAspectRatio);
        reader.setScaledSize(scaledSize);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        if (error) {
            *error = reader.errorString().isEmpty()
                ? QStringLiteral("Could not decode selected MEGA image.")
                : reader.errorString();
        }
        return false;
    }
    if (image.width() > MegaRepairThumbnailMaxSide || image.height() > MegaRepairThumbnailMaxSide) {
        image = image.scaled(MegaRepairThumbnailMaxSide,
                             MegaRepairThumbnailMaxSide,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    QImageWriter writer(thumbnailPath, "jpg");
    writer.setQuality(MegaRepairThumbnailJpegQuality);
    if (!writer.write(image)) {
        if (error) {
            *error = writer.errorString().isEmpty()
                ? QStringLiteral("Could not encode generated MEGA thumbnail.")
                : writer.errorString();
        }
        return false;
    }
    return true;
}

int megaUploadConcurrency()
{
    bool ok = false;
    const int requested = qEnvironmentVariableIntValue("FMQML_MEGA_UPLOAD_CONCURRENCY", &ok);
    if (!ok) {
        return DefaultMegaUploadConcurrency;
    }
    return std::clamp(requested, 1, MaxMegaUploadConcurrency);
}

int megaDownloadConcurrency()
{
    bool ok = false;
    const int requested = qEnvironmentVariableIntValue("FMQML_MEGA_DOWNLOAD_CONCURRENCY", &ok);
    if (!ok) {
        return DefaultMegaDownloadConcurrency;
    }
    return std::clamp(requested, 1, MaxMegaDownloadConcurrency);
}

bool megaDownloadQuotaError(const QString &errorString)
{
    return errorString.compare(QStringLiteral("MEGA transfer or storage quota exceeded"), Qt::CaseInsensitive) == 0;
}

QString megaByteSizeText(qint64 size)
{
    return size >= 0 ? QLocale().formattedDataSize(size) : QStringLiteral("unknown");
}

QVariantList megaAccountStatusProperties()
{
    const qint64 used = megaClient().accountStorageUsedBytes() >= 0
        ? megaClient().accountStorageUsedBytes()
        : MegaCache::accountStorageUsedBytes();
    const qint64 total = megaClient().accountStorageMaxBytes();

    QString storageValue;
    if (total >= 0) {
        storageValue = QStringLiteral("%1 / %2").arg(megaByteSizeText(used), megaByteSizeText(total));
    } else {
        storageValue = megaByteSizeText(used);
    }

    return QVariantList{
        QVariantMap{
            {QStringLiteral("label"), QStringLiteral("Signed in")},
            {QStringLiteral("value"), megaClient().isAccountAuthenticated() ? QStringLiteral("Yes") : QStringLiteral("No")},
        },
        QVariantMap{
            {QStringLiteral("label"), QStringLiteral("Account")},
            {QStringLiteral("value"), megaClient().accountEmail().isEmpty() ? MegaAuth::savedEmail() : megaClient().accountEmail()},
        },
        QVariantMap{
            {QStringLiteral("label"), QStringLiteral("Saved session")},
            {QStringLiteral("value"), MegaAuth::savedSession().isEmpty() ? QStringLiteral("No") : QStringLiteral("Yes")},
        },
        QVariantMap{
            {QStringLiteral("label"), QStringLiteral("Access mode")},
            {QStringLiteral("value"), QStringLiteral("Read-write account access")},
        },
        QVariantMap{
            {QStringLiteral("label"), QStringLiteral("Storage usage")},
            {QStringLiteral("value"), storageValue},
        },
    };
}

QVariantMap megaStorageInfoMap()
{
    const qint64 used = megaClient().accountStorageUsedBytes() >= 0
        ? megaClient().accountStorageUsedBytes()
        : MegaCache::accountStorageUsedBytes();
    const qint64 total = megaClient().accountStorageMaxBytes();
    const qint64 free = (total >= 0 && used >= 0) ? (total - used) : -1;
    const double percent = (total > 0 && used >= 0) ? (static_cast<double>(used) / total) : 0.0;
    const bool valid = (total >= 0 && used >= 0);
    const bool isCritical = valid && total > 0 && free >= 0 && (static_cast<double>(free) / static_cast<double>(total)) < 0.10;

    return {
        {QStringLiteral("valid"), valid},
        {QStringLiteral("total"), total},
        {QStringLiteral("free"), free},
        {QStringLiteral("used"), used},
        {QStringLiteral("percent"), percent},
        {QStringLiteral("totalStr"), megaByteSizeText(total)},
        {QStringLiteral("freeStr"), megaByteSizeText(free)},
        {QStringLiteral("usedStr"), megaByteSizeText(used)},
        {QStringLiteral("fs"), QStringLiteral("MEGA")},
        {QStringLiteral("isCritical"), isCritical},
    };
}

QVariantMap runBlockingMegaAuthorization(const std::function<int()> &startAuthorization,
                                         const QString &successMessage,
                                         const QString &startFailureMessage)
{
    MegaClientInterface &client = megaClient();
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    bool finished = false;
    bool signedIn = false;
    bool timedOut = false;
    QString accountEmail;

    const QMetaObject::Connection authConnection = QObject::connect(
        &client, &MegaClientInterface::accountAuthorizationChanged,
        &loop, [&](bool changedSignedIn, const QString &changedEmail, const QString &) {
            finished = true;
            signedIn = changedSignedIn;
            accountEmail = changedEmail;
            loop.quit();
        });
    const QMetaObject::Connection timeoutConnection = QObject::connect(
        &timeout, &QTimer::timeout,
        &loop, [&]() {
            finished = true;
            timedOut = true;
            signedIn = client.isAccountAuthenticated();
            accountEmail = client.accountEmail();
            loop.quit();
        });

    const int startResult = startAuthorization();
    if (startResult != 0) {
        QObject::disconnect(authConnection);
        QObject::disconnect(timeoutConnection);
        return {
            {QStringLiteral("ok"), false},
            {QStringLiteral("title"), QStringLiteral("MEGA")},
            {QStringLiteral("message"), startFailureMessage},
        };
    }

    if (!finished) {
        timeout.start(60000);
        loop.exec();
    }

    QObject::disconnect(authConnection);
    QObject::disconnect(timeoutConnection);

    const bool ok = signedIn || client.isAccountAuthenticated();
    const QString email = accountEmail.isEmpty() ? client.accountEmail() : accountEmail;
    if (ok) {
        const QString session = client.accountSessionToken();
        if (session.trimmed().isEmpty()) {
            qWarning() << "[MegaFileProvider] MEGA authorization succeeded but SDK session token is empty";
        } else if (!MegaAuth::rememberAuthorization(session, email)) {
            qWarning() << "[MegaFileProvider] Could not persist MEGA authorization in the platform credential store";
        }
    }
    return {
        {QStringLiteral("ok"), ok},
        {QStringLiteral("title"), QStringLiteral("MEGA")},
        {QStringLiteral("message"), ok
            ? successMessage
            : (timedOut
                ? QStringLiteral("MEGA sign in did not complete before the timeout.")
                : QStringLiteral("MEGA sign in failed."))},
        {QStringLiteral("signedIn"), ok},
        {QStringLiteral("accountEmail"), email},
        {QStringLiteral("accountLabel"), email.isEmpty() ? QStringLiteral("Signed in") : email},
        {QStringLiteral("refreshCurrentPath"), ok},
    };
}

CleanupManagedTemporaryFile::CleanupManagedTemporaryFile(const QString &fileTemplate)
    : QTemporaryFile(fileTemplate)
{
    setAutoRemove(false);
}

CleanupManagedTemporaryFile::~CleanupManagedTemporaryFile()
{
    close();
    if (!m_cleanupLeaseId.isEmpty()) {
        CleanupSubsystem::instance().scheduleDelete(m_cleanupLeaseId);
    }
}

void CleanupManagedTemporaryFile::setCleanupLeaseId(const QString &leaseId)
{
    m_cleanupLeaseId = leaseId;
}

QString CleanupManagedTemporaryFile::cleanupLeaseId() const
{
    return m_cleanupLeaseId;
}

QString megaOpenReadStagingRoot(const QString &stagingParentPath, const QString &sourcePath)
{
    const QString resolved = StagingLocationPolicy::resolveStagingParentDirectory(
        stagingParentPath,
        sourcePath,
        stagingParentPath,
        true);
    if (resolved.isEmpty()) {
        return {};
    }

    const QString root = QDir(resolved).filePath(QStringLiteral("mega-openread"));
    return QDir().mkpath(root) ? root : QString{};
}

bool waitForMegaMutation(const std::function<qint64()> &startMutation,
                         const QString &operation,
                         const QString &path,
                         QString *resultPath,
                         QString *errorStr)
{
    QMutex waitMutex;
    QWaitCondition waitCondition;
    bool finished = false;
    bool success = false;
    QString operationError;
    QString operationResultPath;
    qint64 requestId = 0;

    MegaClientInterface &client = megaClient();
    const QMetaObject::Connection finishedConn = QObject::connect(
        &client, &MegaClientInterface::mutationFinished,
        &client,
        [&](qint64 emittedRequestId,
            const QString &emittedOperation,
            const QString &emittedPath,
            bool emittedSuccess,
            const QString &emittedError,
            const QString &emittedResultPath) {
            if ((requestId > 0 && emittedRequestId != requestId)
                || emittedOperation != operation
                || MegaPath::normalizedPath(emittedPath) != MegaPath::normalizedPath(path)) {
                return;
            }
            {
                QMutexLocker waitLocker(&waitMutex);
                finished = true;
                success = emittedSuccess;
                operationError = emittedError;
                operationResultPath = emittedResultPath;
            }
            waitCondition.wakeAll();
        },
        Qt::DirectConnection);

    requestId = startMutation ? startMutation() : 0;
    if (requestId <= 0) {
        QObject::disconnect(finishedConn);
        if (errorStr) {
            *errorStr = QStringLiteral("Could not start MEGA %1 operation").arg(operation);
        }
        return false;
    }

    bool timedOut = false;
    {
        QMutexLocker waitLocker(&waitMutex);
        if (!finished) {
            timedOut = !waitCondition.wait(&waitMutex, 30 * 60 * 1000);
        }
    }

    QObject::disconnect(finishedConn);
    if (timedOut) {
        megaClient().cancelAll();
        operationError = QStringLiteral("MEGA %1 operation timed out").arg(operation);
    }
    if (!finished || !success) {
        if (errorStr) {
            *errorStr = operationError.isEmpty()
                ? QStringLiteral("Unknown MEGA %1 error").arg(operation)
                : operationError;
        }
        return false;
    }
    if (resultPath) {
        *resultPath = operationResultPath;
    }
    if (errorStr) {
        errorStr->clear();
    }
    return true;
}

} // namespace MegaProviderRuntime
