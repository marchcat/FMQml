bool LocalFileProvider::movePath(const QString &sourcePath, const QString &destinationPath) const
{
    clearLastError();
    if (sourcePath.isEmpty() || destinationPath.isEmpty()) {
        setLastError(QStringLiteral("Cannot move item: the source or destination path is empty"));
        return false;
    }
    if (QFileInfo::exists(destinationPath)) {
        setLastError(QStringLiteral("Cannot move %1: an item with the same name already exists")
                         .arg(QDir::toNativeSeparators(sourcePath)));
        return false;
    }
#ifdef Q_OS_WIN
    if (!MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(sourcePath).utf16()),
                     reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(destinationPath).utf16()),
                     MOVEFILE_COPY_ALLOWED)) {
        setLastError(windowsMutationErrorMessage(FileMutationKind::Move, sourcePath, GetLastError()));
        return false;
    }
    return true;
#else
    if (!QFile::rename(sourcePath, destinationPath)) {
        setLastError(QStringLiteral("Cannot move %1").arg(QDir::toNativeSeparators(sourcePath)));
        return false;
    }
    return true;
#endif
}

std::unique_ptr<QIODevice> LocalFileProvider::openRead(const QString &path) const
{
    clearLastError();
    auto file = std::make_unique<QFile>(path);
    if (file->open(QIODevice::ReadOnly)) {
        return std::unique_ptr<QIODevice>(file.release());
    }

#ifdef Q_OS_LINUX
    if (!LinuxAdminBroker::activeSessionNonce().isEmpty()) {
        QString leaseId;
        const QString stagingDirectory = CleanupSubsystem::instance().allocateStagingDirectory(
            CleanupArtifactKind::RemotePreview,
            StagingLocationPolicy::defaultCleanupRoot(),
            QUuid::createUuid().toString(QUuid::WithoutBraces),
            &leaseId);
        if (!stagingDirectory.isEmpty() && !leaseId.isEmpty()) {
            const QString stagedPath = QDir(stagingDirectory).filePath(
                QFileInfo(path).fileName().isEmpty() ? QStringLiteral("admin-read") : QFileInfo(path).fileName());
            QString error;
            if (copyToLocalFileForPreview(path, stagedPath, {}, &error)) {
                auto stagedFile = std::make_unique<AdminStagedReadFile>(stagedPath, leaseId);
                if (stagedFile->open(QIODevice::ReadOnly)) {
                    return std::unique_ptr<QIODevice>(stagedFile.release());
                }
                error = stagedFile->errorString();
            }
            CleanupSubsystem::instance().scheduleDeleteOnFailure(leaseId);
            setLastError(QStringLiteral("Cannot read %1: %2")
                             .arg(QDir::toNativeSeparators(path), error));
            return nullptr;
        }
    }
#endif

    setLastError(QStringLiteral("Cannot read %1: %2")
                     .arg(QDir::toNativeSeparators(path), file->errorString()));
    return nullptr;
}

bool LocalFileProvider::copyToLocalFileForPreview(
    const QString &sourcePath,
    const QString &destinationFilePath,
    const std::function<bool(qint64 processedBytes, qint64 totalBytes)> &progress,
    QString *error) const
{
    QFile source(sourcePath);
    if (source.open(QIODevice::ReadOnly)) {
        QFile destination(destinationFilePath);
        if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = destination.errorString();
            return false;
        }
        const qint64 total = source.size();
        qint64 processed = 0;
        while (!source.atEnd()) {
            const QByteArray chunk = source.read(1024 * 1024);
            if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
                if (error) *error = source.errorString();
                return false;
            }
            if (destination.write(chunk) != chunk.size()) {
                if (error) *error = destination.errorString();
                return false;
            }
            processed += chunk.size();
            if (progress && !progress(processed, total)) {
                if (error) *error = QStringLiteral("Preview materialization cancelled");
                return false;
            }
        }
        destination.setPermissions(QFileDevice::ReadOwner);
        return true;
    }

#ifdef Q_OS_LINUX
    const QString nonce = LinuxAdminBroker::activeSessionNonce();
    if (!nonce.isEmpty()) {
        QFile destination(destinationFilePath);
        if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = destination.errorString();
            return false;
        }
        qint64 offset = 0;
        qint64 totalSize = -1;
        while (totalSize < 0 || offset < totalSize) {
            LinuxAdminBroker::Request request;
            request.operation = LinuxAdminBroker::Operation::ReadFile;
            request.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            request.sessionNonce = nonce;
            request.sourcePath = sourcePath;
            request.offset = offset;
            request.length = 1024 * 1024;
            LinuxAdminBroker broker;
            const LinuxAdminBroker::Result result = broker.submitBlocking(request);
            if (!result.success) {
                if (error) *error = result.errorMessage;
                destination.close();
                QFile::remove(destinationFilePath);
                return false;
            }
            totalSize = result.totalSize;
            if (destination.write(result.data) != result.data.size()) {
                if (error) *error = destination.errorString();
                destination.close();
                QFile::remove(destinationFilePath);
                return false;
            }
            offset += result.data.size();
            if (progress && !progress(offset, totalSize)) {
                if (error) *error = QStringLiteral("Preview materialization cancelled");
                destination.close();
                QFile::remove(destinationFilePath);
                return false;
            }
            if (result.data.isEmpty()) break;
        }
        destination.close();
        QFile::setPermissions(destinationFilePath, QFileDevice::ReadOwner);
        return true;
    }
#endif
    if (error) *error = source.errorString();
    return false;
}

std::unique_ptr<QIODevice> LocalFileProvider::openWrite(const QString &path, bool truncate) const
{
    clearLastError();
    auto file = std::make_unique<QFile>(path);
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    if (truncate) {
        mode |= QIODevice::Truncate;
    }
    if (!file->open(mode)) {
        setLastError(QStringLiteral("Cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(path), file->errorString()));
        return nullptr;
    }
    return std::unique_ptr<QIODevice>(file.release());
}

