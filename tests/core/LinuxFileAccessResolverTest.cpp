#include "FileAccessResolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <sys/stat.h>

namespace {

int fail(const QString &message)
{
    QTextStream(stderr) << message << '\n';
    return 1;
}

QVariantMap rowByLabel(const QVariantList &rows, const QString &label)
{
    for (const QVariant &rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        if (row.value(QStringLiteral("label")).toString() == label) {
            return row;
        }
    }
    return {};
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return fail(QStringLiteral("failed to create temp dir"));
    }

    const QString filePath = tempDir.filePath(QStringLiteral("sample.sh"));
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return fail(QStringLiteral("failed to create test file"));
    }
    file.write("#!/bin/sh\nexit 0\n");
    file.close();

    if (::chmod(QFile::encodeName(filePath).constData(), 0750) != 0) {
        return fail(QStringLiteral("failed to chmod test file"));
    }

    const FileCapabilityInfo fileInfo = FileAccessResolver::resolve(filePath);
    if (!fileInfo.unixInfo.available) {
        return fail(QStringLiteral("unix info was not populated"));
    }
    if (fileInfo.unixInfo.modeOctal != QLatin1String("750")) {
        return fail(QStringLiteral("unexpected file octal mode: %1").arg(fileInfo.unixInfo.modeOctal));
    }
    if (fileInfo.unixInfo.modeString.size() != 10 || !fileInfo.unixInfo.modeString.endsWith(QLatin1String("r-x---"))) {
        return fail(QStringLiteral("unexpected file mode string: %1").arg(fileInfo.unixInfo.modeString));
    }
    if (!fileInfo.access.canRead || !fileInfo.access.canExecute || !fileInfo.access.canDelete) {
        return fail(QStringLiteral("expected test file to be readable, executable, and deletable"));
    }

    const QVariantList unixRows = FileAccessResolver::unixProperties(fileInfo);
    if (rowByLabel(unixRows, QStringLiteral("Owner")).isEmpty()
            || rowByLabel(unixRows, QStringLiteral("Group")).isEmpty()
            || rowByLabel(unixRows, QStringLiteral("Mode")).isEmpty()
            || rowByLabel(unixRows, QStringLiteral("Octal")).value(QStringLiteral("value")).toString() != QLatin1String("750")) {
        return fail(QStringLiteral("unix property rows are incomplete"));
    }

    const QString dirPath = tempDir.filePath(QStringLiteral("folder"));
    if (!QDir().mkdir(dirPath)) {
        return fail(QStringLiteral("failed to create test directory"));
    }
    if (::chmod(QFile::encodeName(dirPath).constData(), 0700) != 0) {
        return fail(QStringLiteral("failed to chmod test directory"));
    }

    const FileCapabilityInfo dirInfo = FileAccessResolver::resolve(dirPath);
    if (!dirInfo.isDirectory || dirInfo.unixInfo.modeOctal != QLatin1String("700")) {
        return fail(QStringLiteral("unexpected directory unix info"));
    }
    if (!dirInfo.access.canBrowse || !dirInfo.access.canTraverse || !dirInfo.access.canCreateChildren) {
        return fail(QStringLiteral("expected directory browse/traverse/create access"));
    }

    return 0;
}
