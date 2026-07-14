#include "MegaProviderActions.h"
#include "MegaFileProvider.h"
#include "MegaProviderRuntime.h"
#include "MegaPath.h"
#include "MegaCache.h"
#include "MegaAuth.h"
#include "MegaClientInterface.h"
#include "FileProvider.h"
#include "CleanupSubsystem.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
using namespace MegaProviderRuntime;
namespace {
constexpr QLatin1StringView MegaSignOutAction{"signOut"};
constexpr QLatin1StringView MegaSignInAction{"signIn"};
constexpr QLatin1StringView MegaAuthStatusAction{"authStatus"};
constexpr QLatin1StringView MegaRepairThumbnailAction{"repairThumbnail"};
}

QList<FileActionDescriptor> megaActionsForContext(const FileActionContext &context)
{
    const QString targetPath = MegaPath::normalizedPath(context.targetPath);
    if (!MegaPath::isSchemePath(targetPath)) {
        return {};
    }

    QList<FileActionDescriptor> actions;
    FileActionDescriptor status;
    status.id = QString(MegaAuthStatusAction);
    status.text = QStringLiteral("MEGA account status");
    status.iconSource = QStringLiteral("../assets/icons/info.svg");
    status.order = 900;
    actions.append(status);

    if (megaClient().isAccountAuthenticated()) {
        FileActionDescriptor signOut;
        signOut.id = QString(MegaSignOutAction);
        signOut.text = QStringLiteral("Sign out from MEGA");
        signOut.iconSource = QStringLiteral("../assets/icons/exit.svg");
        signOut.order = 910;
        actions.append(signOut);

        if (MegaPath::linkIdForPath(targetPath).isEmpty()) {
            const std::optional<FileEntry> entry = MegaCache::getEntry(targetPath);
            if (entry && isMegaThumbnailRepairCandidate(*entry)) {
                FileActionDescriptor repair;
                repair.id = QString(MegaRepairThumbnailAction);
                repair.text = QStringLiteral("Repair MEGA thumbnail");
                repair.iconSource = QStringLiteral("../assets/icons/image.svg");
                repair.order = 320;
                repair.asynchronous = true;
                actions.append(repair);
            }
        }
    } else {
        FileActionDescriptor signIn;
        signIn.id = QString(MegaSignInAction);
        signIn.text = QStringLiteral("Sign in to MEGA");
        signIn.iconSource = QStringLiteral("../assets/icons/plugin.svg");
        signIn.order = 905;
        actions.append(signIn);
    }
    return actions;
}

QVariantMap triggerMegaAction(const QString &actionId, const FileActionContext &context)
{
    if (actionId == MegaAuthStatusAction) {
        const bool signedIn = megaClient().isAccountAuthenticated();
        if (signedIn) {
            megaClient().requestAccountDetails();
        }
        const QString accountEmail = megaClient().accountEmail().isEmpty()
            ? MegaAuth::savedEmail()
            : megaClient().accountEmail();
        const QString label = signedIn
            ? (accountEmail.isEmpty() ? QStringLiteral("Signed in") : accountEmail)
            : (MegaAuth::savedSession().isEmpty()
                ? QStringLiteral("Not signed in")
                : (accountEmail.isEmpty() ? QStringLiteral("Saved session available") : accountEmail));
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("title"), QStringLiteral("MEGA")},
            {QStringLiteral("subtitle"), QStringLiteral("Account authorization")},
            {QStringLiteral("message"), signedIn
                ? QStringLiteral("MEGA account access is active.")
                : QStringLiteral("MEGA account access is not active.")},
            {QStringLiteral("signedIn"), signedIn},
            {QStringLiteral("accountEmail"), accountEmail},
            {QStringLiteral("accountLabel"), label},
            {QStringLiteral("properties"), megaAccountStatusProperties()},
        };
    }

    if (actionId == MegaSignInAction) {
        const QString session = context.parameters.value(QStringLiteral("session")).toString().trimmed();
        const QString email = context.parameters.value(QStringLiteral("email")).toString().trimmed();
        const QString password = context.parameters.value(QStringLiteral("password")).toString();

        if (!session.isEmpty()) {
            return runBlockingMegaAuthorization(
                [session]() { return megaClient().resumeAccountSession(session); },
                QStringLiteral("MEGA session was resumed."),
                QStringLiteral("Could not start MEGA session resume."));
        }

        if (email.isEmpty() || password.isEmpty()) {
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("title"), QStringLiteral("MEGA")},
                {QStringLiteral("message"), QStringLiteral("MEGA sign in requires email and password parameters.")},
                {QStringLiteral("requiresInput"), true},
                {QStringLiteral("inputKind"), QStringLiteral("megaCredentials")},
            };
        }

        return runBlockingMegaAuthorization(
            [email, password]() { return megaClient().loginToAccount(email, password); },
            QStringLiteral("MEGA sign in completed."),
            QStringLiteral("Could not start MEGA sign in."));
    }

    if (actionId == MegaRepairThumbnailAction) {
        const QString normalized = MegaPath::normalizedPath(context.targetPath);
        if (megaThumbnailTraceEnabled()) {
            qInfo().noquote() << "[MegaThumbnailRepair] start" << "path=" << normalized;
        }
        if (!MegaPath::isSchemePath(normalized) || !MegaPath::linkIdForPath(normalized).isEmpty()) {
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("statusOnly"), true},
                {QStringLiteral("title"), QStringLiteral("MEGA")},
                {QStringLiteral("message"), QStringLiteral("MEGA thumbnail repair is available only for account files.")},
            };
        }

        const std::optional<FileEntry> entry = MegaCache::getEntry(normalized);
        if (!entry || !isMegaThumbnailRepairCandidate(*entry)) {
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("statusOnly"), true},
                {QStringLiteral("title"), QStringLiteral("MEGA")},
                {QStringLiteral("message"), QStringLiteral("MEGA thumbnail repair is available only for image files.")},
            };
        }

        const QString stagingRoot = StagingLocationPolicy::resolveStagingParentDirectory(
            QString(), normalized, QString(), true);
        if (stagingRoot.isEmpty()) {
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("statusOnly"), true},
                {QStringLiteral("title"), QStringLiteral("MEGA")},
                {QStringLiteral("message"), QStringLiteral("Could not create MEGA thumbnail repair staging directory.")},
            };
        }

        QTemporaryDir repairDir(QDir(stagingRoot).filePath(QStringLiteral("mega-thumbnail-repair-XXXXXX")));
        if (!repairDir.isValid()) {
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("statusOnly"), true},
                {QStringLiteral("title"), QStringLiteral("MEGA")},
                {QStringLiteral("message"), QStringLiteral("Could not create MEGA thumbnail repair temporary directory.")},
            };
        }

        std::unique_ptr<FileProvider> provider = createMegaFileProvider();
        QString copyError;
        const QString sourcePath = QDir(repairDir.path()).filePath(entry->name.isEmpty()
            ? QStringLiteral("source-image")
            : entry->name);
        if (!provider->copyToLocalFile(normalized, sourcePath, nullptr, &copyError)) {
            if (megaThumbnailTraceEnabled()) {
                qInfo().noquote()
                    << "[MegaThumbnailRepair] source-download-failed"
                    << "path=" << normalized
                    << "error=" << copyError;
            }
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("statusOnly"), true},
                {QStringLiteral("title"), QStringLiteral("MEGA")},
                {QStringLiteral("message"), copyError.isEmpty()
                    ? QStringLiteral("Could not download selected MEGA image for thumbnail repair.")
                    : copyError},
            };
        }

        if (megaThumbnailTraceEnabled()) {
            qInfo().noquote()
                << "[MegaThumbnailRepair] source-downloaded"
                << "path=" << normalized
                << "bytes=" << QFileInfo(sourcePath).size();
        }

        QString thumbnailError;
        const QString thumbnailPath = QDir(repairDir.path()).filePath(QStringLiteral("mega-thumbnail.jpg"));
        if (!writeMegaRepairThumbnailFile(sourcePath, thumbnailPath, &thumbnailError)) {
            if (megaThumbnailTraceEnabled()) {
                qInfo().noquote()
                    << "[MegaThumbnailRepair] encode-failed"
                    << "path=" << normalized
                    << "error=" << thumbnailError;
            }
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("statusOnly"), true},
                {QStringLiteral("title"), QStringLiteral("MEGA")},
                {QStringLiteral("message"), thumbnailError},
            };
        }

        if (megaThumbnailTraceEnabled()) {
            qInfo().noquote()
                << "[MegaThumbnailRepair] encoded"
                << "path=" << normalized
                << "bytes=" << QFileInfo(thumbnailPath).size();
        }

        QString setError;
        if (!megaClient().setNodeThumbnail(normalized, thumbnailPath, 10000, &setError)) {
            if (megaThumbnailTraceEnabled()) {
                qInfo().noquote()
                    << "[MegaThumbnailRepair] upload-failed"
                    << "path=" << normalized
                    << "error=" << setError;
            }
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("statusOnly"), true},
                {QStringLiteral("title"), QStringLiteral("MEGA")},
                {QStringLiteral("message"), setError.isEmpty()
                    ? QStringLiteral("Could not upload generated MEGA thumbnail.")
                    : setError},
            };
        }
        if (megaThumbnailTraceEnabled()) {
            qInfo().noquote() << "[MegaThumbnailRepair] upload-ok" << "path=" << normalized;
        }
        rememberRepairedMegaThumbnail(normalized, thumbnailPath);

        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("statusOnly"), true},
            {QStringLiteral("title"), QStringLiteral("MEGA")},
            {QStringLiteral("message"), QStringLiteral("MEGA thumbnail repaired.")},
            {QStringLiteral("thumbnailInvalidationPaths"), QStringList{normalized}},
        };
    }

    if (actionId == MegaSignOutAction) {
        QString error;
        const bool ok = megaClient().logoutAccount(&error);
        if (ok) {
            MegaAuth::clearSavedAuthorization();
        }
        return {
            {QStringLiteral("ok"), ok},
            {QStringLiteral("title"), QStringLiteral("MEGA")},
            {QStringLiteral("message"), ok ? QStringLiteral("MEGA authorization was removed.") : error},
            {QStringLiteral("refreshCurrentPath"), ok},
        };
    }

    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("title"), QStringLiteral("MEGA")},
        {QStringLiteral("message"), QStringLiteral("Unknown MEGA action.")},
    };
}
