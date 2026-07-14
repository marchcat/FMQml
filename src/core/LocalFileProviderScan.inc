void LocalFileProvider::setShowHidden(bool show)
{
    m_showHidden = show;
}

void LocalFileProvider::pruneFinishedScans()
{
    const qsizetype before = m_scanFutures.size();
    for (qsizetype i = m_scanFutures.size() - 1; i >= 0; --i) {
        if (m_scanFutures.at(i).isFinished()) {
            m_scanFutures.removeAt(i);
        }
    }
    if (before != m_scanFutures.size()) {
        traceLocalProviderNav("pruneFinishedScans", m_currentPath,
                              QStringLiteral("before=%1 after=%2").arg(before).arg(m_scanFutures.size()));
    }
}

void LocalFileProvider::scan(const QString &path)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    const qsizetype futuresBefore = m_scanFutures.size();
    cancel();
    pruneFinishedScans();

    const int myGen = ++m_scanGeneration;
    m_currentPath = path;
    const bool showHidden = m_showHidden;

    traceLocalProviderNav("scan-begin", path,
                          QStringLiteral("generation=%1 showHidden=%2 futuresBefore=%3 futuresAfterPrune=%4")
                              .arg(myGen)
                              .arg(showHidden)
                              .arg(futuresBefore)
                              .arg(m_scanFutures.size()));

    emit started();

    m_scanFutures.append(QtConcurrent::run([this, path, myGen, showHidden]() {
        QElapsedTimer workerTimer;
        workerTimer.start();
        traceLocalProviderNav("scan-worker-begin", path,
                              QStringLiteral("generation=%1 showHidden=%2").arg(myGen).arg(showHidden));

        QElapsedTimer validationTimer;
        validationTimer.start();
        QFileInfo info(path);
#ifdef Q_OS_LINUX
        const bool localDirectoryKnown = info.exists() && info.isDir();
        const bool adminSessionAvailable = !LinuxAdminBroker::activeSessionNonce().isEmpty();
        traceLocalProviderNav("scan-worker-preflight", path,
                              QStringLiteral("generation=%1 exists=%2 isDir=%3 readable=%4 executable=%5 localKnown=%6 adminSession=%7")
                                  .arg(myGen)
                                  .arg(info.exists())
                                  .arg(info.isDir())
                                  .arg(info.isReadable())
                                  .arg(info.isExecutable())
                                  .arg(localDirectoryKnown)
                                  .arg(adminSessionAvailable));
        // An inaccessible parent makes QFileInfo report its children as
        // missing.  In an active admin session the helper is the authority for
        // existence and type, so continue to the normal-enumeration/admin
        // fallback below instead of rejecting the path here.
        if (!localDirectoryKnown && !adminSessionAvailable) {
#else
        if (!info.exists() || !info.isDir()) {
#endif
            if (myGen == m_scanGeneration.load()) {
                emit finished(path, false, myGen, QStringLiteral("Folder does not exist"));
            }
            traceLocalProviderNav("scan-worker-end", path,
                                  QStringLiteral("generation=%1 result=missing validationMs=%2 elapsedMs=%3")
                                      .arg(myGen)
                                      .arg(validationTimer.elapsed())
                                      .arg(workerTimer.elapsed()));
            return;
        }

        const QString resolvedCanonicalPath = info.canonicalFilePath();
        const QString canonicalPath = resolvedCanonicalPath.isEmpty()
            ? normalizedFilesystemPath(path)
            : resolvedCanonicalPath;
        QDir dir(canonicalPath);
#ifndef Q_OS_LINUX
        if (!dir.isReadable()) {
            if (myGen == m_scanGeneration.load()) {
                emit finished(path, false, myGen, QStringLiteral("Folder is not readable"));
            }
            traceLocalProviderNav("scan-worker-end", path,
                                  QStringLiteral("generation=%1 result=not-readable validationMs=%2 elapsedMs=%3")
                                      .arg(myGen)
                                      .arg(validationTimer.elapsed())
                                      .arg(workerTimer.elapsed()));
            return;
        }
#else
        if (!dir.isReadable()) {
            traceLocalProviderNav("scan-worker-admin-candidate", canonicalPath,
                                  QStringLiteral("generation=%1 validationMs=%2")
                                      .arg(myGen)
                                      .arg(validationTimer.elapsed()));
        }
#endif
        traceLocalProviderNav("scan-worker-validated", canonicalPath,
                              QStringLiteral("generation=%1 validationMs=%2 elapsedMs=%3")
                                  .arg(myGen)
                                  .arg(validationTimer.elapsed())
                                  .arg(workerTimer.elapsed()));

#ifdef Q_OS_WIN
        {
        QElapsedTimer nativeOpenTimer;
        nativeOpenTimer.start();
        const QString pattern = windowsSearchPattern(canonicalPath);
        WIN32_FIND_DATAW findData{};
        HANDLE handle = findFirstFileBasic(pattern, &findData);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD errorCode = GetLastError();
            if (errorCode == ERROR_FILE_NOT_FOUND || errorCode == ERROR_NO_MORE_FILES) {
                emit finished(canonicalPath, true, myGen);
                traceLocalProviderNav("scan-worker-finished", canonicalPath,
                                      QStringLiteral("generation=%1 result=empty openMs=%2 elapsedMs=%3")
                                          .arg(myGen)
                                          .arg(nativeOpenTimer.elapsed())
                                          .arg(workerTimer.elapsed()));
                return;
            }
            if (myGen == m_scanGeneration.load()) {
                emit finished(path, false, myGen,
                              windowsMutationErrorMessage(FileMutationKind::Read, canonicalPath, errorCode));
            }
            traceLocalProviderNav("scan-worker-end", canonicalPath,
                                  QStringLiteral("generation=%1 result=findFirst-failed openMs=%2 elapsedMs=%3 error=%4")
                                      .arg(myGen)
                                      .arg(nativeOpenTimer.elapsed())
                                      .arg(workerTimer.elapsed())
                                      .arg(windowsMutationErrorMessage(FileMutationKind::Read, canonicalPath, errorCode)));
            return;
        }
        traceLocalProviderNav("scan-worker-native-open", canonicalPath,
                              QStringLiteral("generation=%1 openMs=%2 elapsedMs=%3")
                                  .arg(myGen)
                                  .arg(nativeOpenTimer.elapsed())
                                  .arg(workerTimer.elapsed()));

        QString parentPrefix = QDir::fromNativeSeparators(canonicalPath);
        if (!parentPrefix.endsWith(QLatin1Char('/'))) {
            parentPrefix += QLatin1Char('/');
        }

        QLocale loc;
        QList<FileEntry> batch;
        batch.reserve(512);
        qsizetype totalEntries = 0;
        QElapsedTimer enumerationTimer;
        enumerationTimer.start();

        do {
            if (myGen != m_scanGeneration.load()) {
                FindClose(handle);
                traceLocalProviderNav("scan-worker-cancelled", canonicalPath,
                                      QStringLiteral("generation=%1 entries=%2 enumerateMs=%3 elapsedMs=%4 currentGeneration=%5")
                                          .arg(myGen)
                                          .arg(totalEntries)
                                          .arg(enumerationTimer.elapsed())
                                          .arg(workerTimer.elapsed())
                                          .arg(m_scanGeneration.load()));
                return;
            }

            const wchar_t *name = findData.cFileName;
            if (name[0] == L'.'
                && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
                continue;
            }

            const bool isHidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0
                || name[0] == L'.';
            if (!showHidden && isHidden) {
                continue;
            }

            batch.append(entryFromFindData(findData, parentPrefix, loc));
            if (batch.size() >= 512) {
                totalEntries += batch.size();
                emit batchReady(batch, myGen);
                traceLocalProviderNav("scan-worker-batch", canonicalPath,
                                      QStringLiteral("generation=%1 batch=512 total=%2 enumerateMs=%3 elapsedMs=%4")
                                          .arg(myGen)
                                          .arg(totalEntries)
                                          .arg(enumerationTimer.elapsed())
                                          .arg(workerTimer.elapsed()));
                batch.clear();
            }
        } while (FindNextFileW(handle, &findData));

        const DWORD findError = GetLastError();
        FindClose(handle);

        if (findError != ERROR_NO_MORE_FILES) {
            if (myGen == m_scanGeneration.load()) {
                emit finished(path, false, myGen,
                              windowsMutationErrorMessage(FileMutationKind::Read, canonicalPath, findError));
            }
            traceLocalProviderNav("scan-worker-end", canonicalPath,
                                  QStringLiteral("generation=%1 result=findNext-failed entries=%2 enumerateMs=%3 elapsedMs=%4 error=%5")
                                      .arg(myGen)
                                      .arg(totalEntries + batch.size())
                                      .arg(enumerationTimer.elapsed())
                                      .arg(workerTimer.elapsed())
                                      .arg(windowsMutationErrorMessage(FileMutationKind::Read, canonicalPath, findError)));
            return;
        }

        if (!batch.isEmpty()) {
            totalEntries += batch.size();
            emit batchReady(batch, myGen);
            traceLocalProviderNav("scan-worker-batch", canonicalPath,
                                  QStringLiteral("generation=%1 batch=final total=%2 enumerateMs=%3 elapsedMs=%4")
                                      .arg(myGen)
                                      .arg(totalEntries)
                                      .arg(enumerationTimer.elapsed())
                                      .arg(workerTimer.elapsed()));
        }

        emit finished(canonicalPath, true, myGen);
        traceLocalProviderNav("scan-worker-finished", canonicalPath,
                              QStringLiteral("generation=%1 result=success entries=%2 enumerateMs=%3 elapsedMs=%4")
                                  .arg(myGen)
                                  .arg(totalEntries)
                                  .arg(enumerationTimer.elapsed())
                                  .arg(workerTimer.elapsed()));
        return;
        }
#endif

#ifdef Q_OS_LINUX
        {
        QElapsedTimer nativeOpenTimer;
        nativeOpenTimer.start();
        const QString displayPath = normalizedFilesystemPath(path);
        QList<LinuxFileEnumerator::Entry> entries;
        QString error;
        LinuxFileEnumerator::Options options;
        options.includeHidden = showHidden;
        bool listedByAdminHelper = false;
        QList<FileEntry> adminEntries;
        const QString adminSessionNonce = LinuxAdminBroker::activeSessionNonce();
        bool localEnumerationSucceeded = false;
        if (adminSessionNonce.isEmpty()) {
            localEnumerationSucceeded =
                LinuxFileEnumerator::enumerateChildren(displayPath, options, &entries, &error);
            traceLocalProviderNav("scan-worker-local-list", displayPath,
                                  QStringLiteral("generation=%1 success=%2 entries=%3 error=%4")
                                      .arg(myGen)
                                      .arg(localEnumerationSucceeded)
                                      .arg(entries.size())
                                      .arg(error));
        }
        if (!localEnumerationSucceeded) {
            LinuxAdminBroker::Request request;
            request.operation = LinuxAdminBroker::Operation::ListDirectory;
            request.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            request.sessionNonce = adminSessionNonce;
            request.sourcePath = displayPath;
            request.includeHidden = showHidden;
            traceLocalProviderNav("scan-worker-admin-submit", displayPath,
                                  QStringLiteral("generation=%1 nonce=%2 showHidden=%3")
                                      .arg(myGen)
                                      .arg(request.sessionNonce.isEmpty() ? QStringLiteral("missing")
                                                                         : QStringLiteral("present"))
                                      .arg(showHidden));
            if (!request.sessionNonce.isEmpty()) {
                LinuxAdminBroker broker;
                const LinuxAdminBroker::Result adminResult = broker.submitBlocking(request);
                traceLocalProviderNav("scan-worker-admin-result", displayPath,
                                      QStringLiteral("generation=%1 success=%2 code=%3 message=%4 failedPath=%5 entries=%6")
                                          .arg(myGen)
                                          .arg(adminResult.success)
                                          .arg(adminResult.errorCode)
                                          .arg(adminResult.errorMessage)
                                          .arg(adminResult.failedPath)
                                          .arg(adminResult.entries.size()));
                if (adminResult.success) {
                    adminEntries = entriesFromAdminHelper(adminResult.entries, QLocale{});
                    listedByAdminHelper = true;
                    traceLocalProviderNav("scan-worker-admin-list", canonicalPath,
                                          QStringLiteral("generation=%1 entries=%2")
                                              .arg(myGen)
                                              .arg(adminEntries.size()));
                } else {
                    error = adminResult.errorMessage.isEmpty() ? error : adminResult.errorMessage;
                }
            }
        }
        if (!listedByAdminHelper && entries.isEmpty() && !error.isEmpty()) {
            if (myGen == m_scanGeneration.load()) {
                emit finished(path, false, myGen,
                              error.isEmpty()
                                  ? posixErrorMessage(FileMutationKind::Read, canonicalPath, EIO)
                                  : error);
            }
            traceLocalProviderNav("scan-worker-end", canonicalPath,
                                  QStringLiteral("generation=%1 result=opendir-failed openMs=%2 elapsedMs=%3 error=%4")
                                      .arg(myGen)
                                      .arg(nativeOpenTimer.elapsed())
                                      .arg(workerTimer.elapsed())
                                      .arg(error));
            return;
        }
        traceLocalProviderNav("scan-worker-native-open", canonicalPath,
                              QStringLiteral("generation=%1 openMs=%2 elapsedMs=%3")
                                  .arg(myGen)
                                  .arg(nativeOpenTimer.elapsed())
                                  .arg(workerTimer.elapsed()));

        QLocale loc;
        QList<FileEntry> batch;
        batch.reserve(512);
        qsizetype totalEntries = 0;
        QElapsedTimer enumerationTimer;
        enumerationTimer.start();

        if (listedByAdminHelper) {
            for (const FileEntry &entry : std::as_const(adminEntries)) {
                batch.append(entry);
                if (batch.size() >= 512) {
                    totalEntries += batch.size();
                    emit batchReady(batch, myGen);
                    batch.clear();
                }
            }
        }

        for (const LinuxFileEnumerator::Entry &entry : std::as_const(entries)) {
            if (myGen != m_scanGeneration.load()) {
                traceLocalProviderNav("scan-worker-cancelled", canonicalPath,
                                      QStringLiteral("generation=%1 entries=%2 enumerateMs=%3 elapsedMs=%4 currentGeneration=%5")
                                          .arg(myGen)
                                          .arg(totalEntries)
                                          .arg(enumerationTimer.elapsed())
                                          .arg(workerTimer.elapsed())
                                          .arg(m_scanGeneration.load()));
                return;
            }

            batch.append(LinuxFileEnumerator::toFileEntry(entry, loc));
            if (batch.size() >= 512) {
                totalEntries += batch.size();
                emit batchReady(batch, myGen);
                traceLocalProviderNav("scan-worker-batch", canonicalPath,
                                      QStringLiteral("generation=%1 batch=512 total=%2 enumerateMs=%3 elapsedMs=%4")
                                          .arg(myGen)
                                          .arg(totalEntries)
                                          .arg(enumerationTimer.elapsed())
                                          .arg(workerTimer.elapsed()));
                batch.clear();
            }
        }

        if (!batch.isEmpty()) {
            totalEntries += batch.size();
            emit batchReady(batch, myGen);
            traceLocalProviderNav("scan-worker-batch", canonicalPath,
                                  QStringLiteral("generation=%1 batch=final total=%2 enumerateMs=%3 elapsedMs=%4")
                                      .arg(myGen)
                                      .arg(totalEntries)
                                      .arg(enumerationTimer.elapsed())
                                      .arg(workerTimer.elapsed()));
        }

        emit finished(displayPath, true, myGen);
        traceLocalProviderNav("scan-worker-finished", canonicalPath,
                              QStringLiteral("generation=%1 result=success entries=%2 enumerateMs=%3 elapsedMs=%4 displayPath=%5")
                                  .arg(myGen)
                                  .arg(totalEntries)
                                  .arg(enumerationTimer.elapsed())
                                  .arg(workerTimer.elapsed())
                                  .arg(QDir::toNativeSeparators(displayPath)));
        return;
        }
#endif

        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
        if (showHidden) {
            filters |= QDir::Hidden;
        }

        QList<FileEntry> batch;
        batch.reserve(512);
        QDirIterator it(dir.absolutePath(), filters);
        qsizetype totalEntries = 0;
        QElapsedTimer enumerationTimer;
        enumerationTimer.start();

        while (it.hasNext()) {
            it.next();
            if (myGen != m_scanGeneration.load()) {
                traceLocalProviderNav("scan-worker-cancelled", canonicalPath,
                                      QStringLiteral("generation=%1 entries=%2 enumerateMs=%3 elapsedMs=%4 currentGeneration=%5")
                                          .arg(myGen)
                                          .arg(totalEntries)
                                          .arg(enumerationTimer.elapsed())
                                          .arg(workerTimer.elapsed())
                                          .arg(m_scanGeneration.load()));
                return;
            }

            QFileInfo fileInfo = it.fileInfo();
            const bool isHidden = fileInfo.isHidden() || fileInfo.fileName().startsWith('.');
            if (!showHidden && isHidden) {
                continue;
            }

            FileEntry entry = entryFromInfo(fileInfo);
            batch.append(entry);

            if (batch.size() >= 512) {
                totalEntries += batch.size();
                emit batchReady(batch, myGen);
                traceLocalProviderNav("scan-worker-batch", canonicalPath,
                                      QStringLiteral("generation=%1 batch=512 total=%2 enumerateMs=%3 elapsedMs=%4")
                                          .arg(myGen)
                                          .arg(totalEntries)
                                          .arg(enumerationTimer.elapsed())
                                          .arg(workerTimer.elapsed()));
                batch.clear();
            }
        }

        if (!batch.isEmpty()) {
            totalEntries += batch.size();
            emit batchReady(batch, myGen);
            traceLocalProviderNav("scan-worker-batch", canonicalPath,
                                  QStringLiteral("generation=%1 batch=final total=%2 enumerateMs=%3 elapsedMs=%4")
                                      .arg(myGen)
                                      .arg(totalEntries)
                                      .arg(enumerationTimer.elapsed())
                                      .arg(workerTimer.elapsed()));
        }

        emit finished(canonicalPath, true, myGen);
        traceLocalProviderNav("scan-worker-finished", canonicalPath,
                              QStringLiteral("generation=%1 result=success entries=%2 enumerateMs=%3 elapsedMs=%4")
                                  .arg(myGen)
                                  .arg(totalEntries)
                                  .arg(enumerationTimer.elapsed())
                                  .arg(workerTimer.elapsed()));
    }));
    traceLocalProviderNav("scan-returned", path,
                          QStringLiteral("generation=%1 futuresNow=%2 elapsedMs=%3")
                              .arg(myGen)
                              .arg(m_scanFutures.size())
                              .arg(totalTimer.elapsed()));
}

void LocalFileProvider::cancel()
{
    const int generation = ++m_scanGeneration;
    traceLocalProviderNav("cancel", m_currentPath,
                          QStringLiteral("generation=%1 futures=%2").arg(generation).arg(m_scanFutures.size()));
}

bool LocalFileProvider::isRunning() const
{
    return std::any_of(m_scanFutures.cbegin(), m_scanFutures.cend(),
                       [](const QFuture<void> &future) {
                           return !future.isFinished();
                       });
}

QString LocalFileProvider::currentPath() const
{
    return m_currentPath;
}

int LocalFileProvider::currentGeneration() const
{
    return m_scanGeneration.load();
}

