#include "LinuxFileEnumerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStack>
#include <QTextStream>

namespace {

int fail(const QString &message)
{
    QTextStream(stderr) << message << '\n';
    return 1;
}

QString defaultProbePath()
{
    const QString home = QDir::homePath();
    const QString bigRar = QDir(home).filePath(QStringLiteral("bigRAR.rar"));
    if (QFileInfo::exists(bigRar)) {
        return bigRar;
    }
    return home;
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString probePath = argc > 1
        ? QDir::cleanPath(QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1])))
        : QDir::cleanPath(defaultProbePath());
    if (probePath.isEmpty() || !QFileInfo::exists(probePath)) {
        return fail(QStringLiteral("probe path does not exist: %1").arg(probePath));
    }

    const std::optional<dev_t> rootDevice = LinuxFileEnumerator::deviceForPath(QStringLiteral("/"));
    if (!rootDevice) {
        return fail(QStringLiteral("could not stat /"));
    }

    LinuxFileEnumerator::Options options;
    options.includeHidden = true;
    options.stayOnRootDevice = true;
    options.rootDevice = *rootDevice;

    QSet<QString> visited;
    QStringList skipped;
    QStack<QString> pending;
    pending.push(QStringLiteral("/"));

    while (!pending.isEmpty()) {
        const QString path = QDir::cleanPath(pending.pop());
        if (visited.contains(path)) {
            continue;
        }
        visited.insert(path);

        QList<LinuxFileEnumerator::Entry> entries;
        QString error;
        if (!LinuxFileEnumerator::enumerateChildren(path, options, &entries, &error)) {
            continue;
        }

        for (const LinuxFileEnumerator::Entry &entry : std::as_const(entries)) {
            const QString entryPath = QDir::cleanPath(entry.path);
            if (entryPath == probePath) {
                return 0;
            }
            if (!probePath.startsWith(entryPath + QLatin1Char('/'))) {
                continue;
            }
            if (entry.isMountBoundary) {
                skipped.append(entryPath);
                continue;
            }
            if (entry.isDirectory && !entry.isSymlink) {
                pending.push(entryPath);
            }
        }
    }

    QTextStream(stderr) << "probe not reachable from / traversal: " << probePath << '\n';
    for (const QString &path : std::as_const(skipped)) {
        QTextStream(stderr) << "skipped boundary on probe path: " << path << '\n';
    }
    return 1;
}
