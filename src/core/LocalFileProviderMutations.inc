bool LocalFileProvider::renamePath(const QString &oldPath, const QString &newName)
{
    clearLastError();
    const QString trimmedName = newName.trimmed();
    if (oldPath.isEmpty() || trimmedName.isEmpty()) {
        setLastError(QStringLiteral("Cannot rename %1: the name is empty")
                         .arg(QDir::toNativeSeparators(oldPath)));
        return false;
    }

    QFileInfo oldInfo(oldPath);
    if (oldInfo.fileName() == trimmedName) {
        return true;
    }

    if (trimmedName.contains('/') || trimmedName.contains('\\')) {
        setLastError(QStringLiteral("Cannot rename %1: the name is invalid")
                         .arg(QDir::toNativeSeparators(oldPath)));
        return false;
    }

    const QString newPath = oldInfo.absoluteDir().filePath(trimmedName);
    if (QFileInfo::exists(newPath) && !sameFilesystemPath(oldPath, newPath)) {
        setLastError(QStringLiteral("Cannot rename %1: an item with the same name already exists")
                         .arg(QDir::toNativeSeparators(oldPath)));
        return false;
    }

#ifdef Q_OS_WIN
    if (!MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(oldPath).utf16()),
                     reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(newPath).utf16()),
                     MOVEFILE_COPY_ALLOWED)) {
        setLastError(windowsMutationErrorMessage(FileMutationKind::Rename, oldPath, GetLastError()));
        return false;
    }
    return true;
#else
    if (!QFile::rename(oldPath, newPath)) {
        setLastError(QStringLiteral("Cannot rename %1").arg(QDir::toNativeSeparators(oldPath)));
        return false;
    }
    return true;
#endif
}

bool LocalFileProvider::createFolder(const QString &parentPath, const QString &name, QString *createdPath)
{
    clearLastError();
    QDir dir(parentPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Cannot create folder in %1")
                         .arg(QDir::toNativeSeparators(parentPath)));
        return false;
    }

    QString folderName = name.trimmed();
    if (folderName.isEmpty()) {
        setLastError(QStringLiteral("Cannot create folder in %1: the name is empty")
                         .arg(QDir::toNativeSeparators(parentPath)));
        return false;
    }

    if (dir.exists(folderName)) {
        for (int i = 1; i < 1000; ++i) {
            const QString candidate = QStringLiteral("%1 (%2)").arg(folderName).arg(i);
            if (!dir.exists(candidate)) {
                folderName = candidate;
                break;
            }
        }
    }

#ifdef Q_OS_WIN
    const QString folderPath = dir.absoluteFilePath(folderName);
    if (!CreateDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(folderPath).utf16()), nullptr)) {
        setLastError(windowsMutationErrorMessage(FileMutationKind::CreateFolder, folderPath, GetLastError()));
        return false;
    }
#else
    if (!dir.mkdir(folderName)) {
        setLastError(QStringLiteral("Cannot create folder in %1").arg(QDir::toNativeSeparators(parentPath)));
        return false;
    }
#endif

    if (createdPath) {
        *createdPath = dir.absoluteFilePath(folderName);
    }
    return true;
}

bool LocalFileProvider::createFile(const QString &parentPath, const QString &name, QString *createdPath)
{
    clearLastError();
    QDir dir(parentPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Cannot create file in %1")
                         .arg(QDir::toNativeSeparators(parentPath)));
        return false;
    }

    QString fileName = name.trimmed();
    if (fileName.isEmpty()) {
        setLastError(QStringLiteral("Cannot create file in %1: the name is empty")
                         .arg(QDir::toNativeSeparators(parentPath)));
        return false;
    }

    if (dir.exists(fileName)) {
        const int dot = fileName.lastIndexOf(QChar('.'));
        const QString base = (dot > 0) ? fileName.left(dot) : fileName;
        const QString ext = (dot > 0) ? fileName.mid(dot) : QString();
        for (int i = 1; i < 1000; ++i) {
            const QString candidate = ext.isEmpty()
                ? QStringLiteral("%1 (%2)").arg(base).arg(i)
                : QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(ext);
            if (!dir.exists(candidate)) {
                fileName = candidate;
                break;
            }
        }
    }

    const QString filePath = dir.absoluteFilePath(fileName);
#ifdef Q_OS_WIN
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(filePath).utf16()),
                                GENERIC_WRITE,
                                FILE_SHARE_READ,
                                nullptr,
                                CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        setLastError(windowsMutationErrorMessage(FileMutationKind::CreateFile, filePath, GetLastError()));
        return false;
    }
    CloseHandle(handle);
#else
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setLastError(QStringLiteral("Cannot create file in %1: %2")
                         .arg(QDir::toNativeSeparators(parentPath), file.errorString()));
        return false;
    }
    file.close();
#endif

    if (createdPath) {
        *createdPath = filePath;
    }
    return true;
}

QString LocalFileProvider::lastErrorString() const
{
    return m_lastError;
}

void LocalFileProvider::clearLastError() const
{
    m_lastError.clear();
}

void LocalFileProvider::setLastError(const QString &error) const
{
    m_lastError = error;
}
