#include "FileAccessResolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStack>
#include <QTextStream>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

struct Options {
    QString root = QStringLiteral("C:/");
    qsizetype limit = 50000;
    QString outputPath = QStringLiteral(".qa-tmp/access-audit.jsonl");
    QString conflictsPath = QStringLiteral(".qa-tmp/access-audit-conflicts.jsonl");
    QString summaryPath = QStringLiteral(".qa-tmp/access-audit-summary.json");
    qsizetype progressEvery = 5000;
    bool descendReparsePoints = false;
    bool writeAllRows = true;
};

struct ProbeResult {
    FileAccessInfo::State state = FileAccessInfo::State::Unknown;
    DWORD error = ERROR_SUCCESS;
    FileAccessInfo::State targetState = FileAccessInfo::State::Unknown;
    DWORD targetError = ERROR_SUCCESS;
    FileAccessInfo::State parentState = FileAccessInfo::State::Unknown;
    DWORD parentError = ERROR_SUCCESS;
};

struct OperationResult {
    QString name;
    FileAccessInfo::State resolverState = FileAccessInfo::State::Unknown;
    ProbeResult probe;
    bool hardConflict = false;
    bool review = false;
};

struct OperationStats {
    qsizetype total = 0;
    qsizetype matchAllowed = 0;
    qsizetype matchDenied = 0;
    qsizetype resolverAllowedProbeDenied = 0;
    qsizetype resolverDeniedProbeAllowed = 0;
    qsizetype resolverUnknownProbeKnown = 0;
    qsizetype resolverKnownProbeUnknown = 0;
};

struct ScanStats {
    qsizetype scanned = 0;
    qsizetype files = 0;
    qsizetype directories = 0;
    qsizetype others = 0;
    qsizetype reparsePoints = 0;
    qsizetype enumerationErrors = 0;
    qsizetype hardConflicts = 0;
    qsizetype reviewRows = 0;
    QMap<QString, OperationStats> operations;
};

QString stateName(FileAccessInfo::State state)
{
    switch (state) {
    case FileAccessInfo::State::Allowed:
        return QStringLiteral("allowed");
    case FileAccessInfo::State::Denied:
        return QStringLiteral("denied");
    case FileAccessInfo::State::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

QString winErrorName(DWORD error)
{
    switch (error) {
    case ERROR_SUCCESS:
        return QStringLiteral("ERROR_SUCCESS");
    case ERROR_ACCESS_DENIED:
        return QStringLiteral("ERROR_ACCESS_DENIED");
    case ERROR_PRIVILEGE_NOT_HELD:
        return QStringLiteral("ERROR_PRIVILEGE_NOT_HELD");
    case ERROR_SHARING_VIOLATION:
        return QStringLiteral("ERROR_SHARING_VIOLATION");
    case ERROR_LOCK_VIOLATION:
        return QStringLiteral("ERROR_LOCK_VIOLATION");
    case ERROR_FILE_NOT_FOUND:
        return QStringLiteral("ERROR_FILE_NOT_FOUND");
    case ERROR_PATH_NOT_FOUND:
        return QStringLiteral("ERROR_PATH_NOT_FOUND");
    case ERROR_INVALID_PARAMETER:
        return QStringLiteral("ERROR_INVALID_PARAMETER");
    default:
        return QStringLiteral("ERROR_%1").arg(error);
    }
}

QString winErrorMessage(DWORD error)
{
    if (error == ERROR_SUCCESS) {
        return {};
    }

    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
        | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags,
                                        nullptr,
                                        error,
                                        0,
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    QString message;
    if (length > 0 && buffer) {
        message = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
        LocalFree(buffer);
    }
    return message;
}

bool probeAllowed(FileAccessInfo::State state)
{
    return state == FileAccessInfo::State::Allowed;
}

bool probeDenied(FileAccessInfo::State state)
{
    return state == FileAccessInfo::State::Denied;
}

bool probeUnknown(FileAccessInfo::State state)
{
    return state == FileAccessInfo::State::Unknown;
}

bool probeKnown(FileAccessInfo::State state)
{
    return !probeUnknown(state);
}

QJsonObject probeJson(const ProbeResult &probe)
{
    QJsonObject object;
    object.insert(QStringLiteral("state"), stateName(probe.state));
    object.insert(QStringLiteral("error"), static_cast<int>(probe.error));
    object.insert(QStringLiteral("errorName"), winErrorName(probe.error));
    const QString message = winErrorMessage(probe.error);
    if (!message.isEmpty()) {
        object.insert(QStringLiteral("message"), message);
    }
    if (probe.targetState != FileAccessInfo::State::Unknown || probe.targetError != ERROR_SUCCESS
        || probe.parentState != FileAccessInfo::State::Unknown || probe.parentError != ERROR_SUCCESS) {
        object.insert(QStringLiteral("targetState"), stateName(probe.targetState));
        object.insert(QStringLiteral("targetError"), static_cast<int>(probe.targetError));
        object.insert(QStringLiteral("targetErrorName"), winErrorName(probe.targetError));
        object.insert(QStringLiteral("parentState"), stateName(probe.parentState));
        object.insert(QStringLiteral("parentError"), static_cast<int>(probe.parentError));
        object.insert(QStringLiteral("parentErrorName"), winErrorName(probe.parentError));
    }
    return object;
}

QString nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

QString normalizedPath(QString path)
{
    path = QDir::fromNativeSeparators(path);
    if (path.size() == 2 && path.at(1) == QLatin1Char(':')) {
        path.append(QLatin1Char('/'));
    }
    return QDir::cleanPath(path);
}

QString parentPath(const QString &path)
{
    const QFileInfo info(path);
    const QString parent = info.absolutePath();
    if (parent.isEmpty() || QDir::cleanPath(parent).compare(QDir::cleanPath(path), Qt::CaseInsensitive) == 0) {
        return {};
    }
    return parent;
}

QString searchPattern(const QString &directoryPath)
{
    QString pattern = nativePath(directoryPath);
    if (!pattern.endsWith(QLatin1Char('\\')) && !pattern.endsWith(QLatin1Char('/'))) {
        pattern.append(QLatin1Char('\\'));
    }
    pattern.append(QLatin1Char('*'));
    return pattern;
}

bool ensureParentDirectory(const QString &path)
{
    const QFileInfo info(path);
    const QString dir = info.absolutePath();
    return dir.isEmpty() || QDir().mkpath(dir);
}

void writeJsonLine(QFile &file, const QJsonObject &object)
{
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
}

ProbeResult openProbe(const QString &path, ACCESS_MASK desiredAccess, bool isDirectory)
{
    ProbeResult result;
    const std::wstring widePath = nativePath(path).toStdWString();
    const DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const DWORD flags = isDirectory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;
    HANDLE handle = CreateFileW(widePath.c_str(),
                                desiredAccess,
                                shareMode,
                                nullptr,
                                OPEN_EXISTING,
                                flags,
                                nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        result.state = FileAccessInfo::State::Allowed;
        result.error = ERROR_SUCCESS;
        return result;
    }

    result.error = GetLastError();
    switch (result.error) {
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        result.state = FileAccessInfo::State::Denied;
        break;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    default:
        result.state = FileAccessInfo::State::Unknown;
        break;
    }
    return result;
}

ProbeResult deleteProbe(const QString &path, bool isDirectory)
{
    ProbeResult target = openProbe(path, DELETE, isDirectory);
    ProbeResult parentProbe;
    const QString parentDirectory = parentPath(path);
    if (!parentDirectory.isEmpty()) {
        parentProbe = openProbe(parentDirectory, FILE_DELETE_CHILD, true);
    }

    ProbeResult result;
    result.targetState = target.state;
    result.targetError = target.error;
    result.parentState = parentProbe.state;
    result.parentError = parentProbe.error;

    if (probeAllowed(target.state) || probeAllowed(parentProbe.state)) {
        result.state = FileAccessInfo::State::Allowed;
        result.error = ERROR_SUCCESS;
    } else if (probeDenied(target.state) && probeDenied(parentProbe.state)) {
        result.state = FileAccessInfo::State::Denied;
        result.error = probeDenied(target.state) ? target.error : parentProbe.error;
    } else {
        result.state = FileAccessInfo::State::Unknown;
        result.error = target.error != ERROR_SUCCESS ? target.error : parentProbe.error;
    }
    return result;
}

QList<OperationResult> operationsForPath(const QString &path, const FileCapabilityInfo &capabilities)
{
    QList<OperationResult> operations;
    const bool isDirectory = capabilities.isDirectory;

    auto append = [&](const QString &name, FileAccessInfo::State resolverState, const ProbeResult &probe) {
        OperationResult op;
        op.name = name;
        op.resolverState = resolverState;
        op.probe = probe;
        op.hardConflict = (probeAllowed(resolverState) && probeDenied(probe.state))
            || (probeDenied(resolverState) && probeAllowed(probe.state));
        op.review = op.hardConflict || (probeUnknown(resolverState) && probeKnown(probe.state));
        operations.append(op);
    };

    if (isDirectory) {
        append(QStringLiteral("browse"),
               capabilities.access.browseState,
               openProbe(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, true));
        append(QStringLiteral("createInside"),
               capabilities.access.createChildrenState,
               openProbe(path, FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY, true));
        append(QStringLiteral("delete"),
               capabilities.access.deleteState,
               deleteProbe(path, true));
        append(QStringLiteral("traverse"),
               capabilities.access.traverseState,
               openProbe(path, FILE_TRAVERSE, true));
    } else {
        append(QStringLiteral("read"),
               capabilities.access.readState,
               openProbe(path, FILE_READ_DATA | FILE_READ_ATTRIBUTES, false));
        append(QStringLiteral("modify"),
               capabilities.access.modifyState,
               openProbe(path, FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES, false));
        append(QStringLiteral("delete"),
               capabilities.access.deleteState,
               deleteProbe(path, false));
        append(QStringLiteral("execute"),
               capabilities.access.executeState,
               openProbe(path, FILE_EXECUTE, false));
    }

    append(QStringLiteral("changeAttributes"),
           capabilities.access.changeAttributesState,
           openProbe(path, FILE_WRITE_ATTRIBUTES, isDirectory));

    return operations;
}

void updateStats(ScanStats *stats, const QList<OperationResult> &operations, bool *rowConflict, bool *rowReview)
{
    *rowConflict = false;
    *rowReview = false;

    for (const OperationResult &operation : operations) {
        OperationStats &opStats = stats->operations[operation.name];
        ++opStats.total;
        if (operation.resolverState == FileAccessInfo::State::Allowed
            && operation.probe.state == FileAccessInfo::State::Allowed) {
            ++opStats.matchAllowed;
        } else if (operation.resolverState == FileAccessInfo::State::Denied
                   && operation.probe.state == FileAccessInfo::State::Denied) {
            ++opStats.matchDenied;
        } else if (operation.resolverState == FileAccessInfo::State::Allowed
                   && operation.probe.state == FileAccessInfo::State::Denied) {
            ++opStats.resolverAllowedProbeDenied;
        } else if (operation.resolverState == FileAccessInfo::State::Denied
                   && operation.probe.state == FileAccessInfo::State::Allowed) {
            ++opStats.resolverDeniedProbeAllowed;
        } else if (operation.resolverState == FileAccessInfo::State::Unknown
                   && operation.probe.state != FileAccessInfo::State::Unknown) {
            ++opStats.resolverUnknownProbeKnown;
        } else if (operation.resolverState != FileAccessInfo::State::Unknown
                   && operation.probe.state == FileAccessInfo::State::Unknown) {
            ++opStats.resolverKnownProbeUnknown;
        }

        *rowConflict = *rowConflict || operation.hardConflict;
        *rowReview = *rowReview || operation.review;
    }

    if (*rowConflict) {
        ++stats->hardConflicts;
    }
    if (*rowReview) {
        ++stats->reviewRows;
    }
}

QJsonObject auditPath(const QString &path, ScanStats *stats, bool *rowConflict, bool *rowReview)
{
    FileAccessResolver::invalidate(path);
    const FileCapabilityInfo capabilities = FileAccessResolver::resolve(path);
    const QFileInfo info(path);
    const bool isDirectory = capabilities.isDirectory;
    const QList<OperationResult> operations = operationsForPath(path, capabilities);

    updateStats(stats, operations, rowConflict, rowReview);

    ++stats->scanned;
    if (isDirectory) {
        ++stats->directories;
    } else if (capabilities.exists) {
        ++stats->files;
    } else {
        ++stats->others;
    }
    if (info.isSymLink()) {
        ++stats->reparsePoints;
    }

    QJsonObject attrs;
    attrs.insert(QStringLiteral("hidden"), capabilities.attributes.hidden);
    attrs.insert(QStringLiteral("readOnly"), capabilities.attributes.readOnly);
    attrs.insert(QStringLiteral("system"), capabilities.attributes.system);
    attrs.insert(QStringLiteral("archive"), capabilities.attributes.archive);
    attrs.insert(QStringLiteral("symLink"), info.isSymLink());

    QJsonObject opsObject;
    for (const OperationResult &operation : operations) {
        QJsonObject object;
        object.insert(QStringLiteral("resolver"), stateName(operation.resolverState));
        object.insert(QStringLiteral("probe"), probeJson(operation.probe));
        object.insert(QStringLiteral("hardConflict"), operation.hardConflict);
        object.insert(QStringLiteral("review"), operation.review);
        opsObject.insert(operation.name, object);
    }

    QJsonObject row;
    row.insert(QStringLiteral("path"), nativePath(path));
    row.insert(QStringLiteral("type"), isDirectory ? QStringLiteral("directory") : (capabilities.exists ? QStringLiteral("file") : QStringLiteral("other")));
    row.insert(QStringLiteral("resolverExact"), capabilities.access.exact);
    row.insert(QStringLiteral("resolverSummary"), capabilities.accessSummary);
    row.insert(QStringLiteral("attributes"), attrs);
    row.insert(QStringLiteral("operations"), opsObject);
    row.insert(QStringLiteral("hardConflict"), *rowConflict);
    row.insert(QStringLiteral("review"), *rowReview);
    return row;
}

QJsonObject statsJson(const ScanStats &stats, qint64 elapsedMs)
{
    QJsonObject operations;
    for (auto it = stats.operations.constBegin(); it != stats.operations.constEnd(); ++it) {
        const OperationStats &op = it.value();
        QJsonObject object;
        object.insert(QStringLiteral("total"), static_cast<qint64>(op.total));
        object.insert(QStringLiteral("matchAllowed"), static_cast<qint64>(op.matchAllowed));
        object.insert(QStringLiteral("matchDenied"), static_cast<qint64>(op.matchDenied));
        object.insert(QStringLiteral("resolverAllowedProbeDenied"), static_cast<qint64>(op.resolverAllowedProbeDenied));
        object.insert(QStringLiteral("resolverDeniedProbeAllowed"), static_cast<qint64>(op.resolverDeniedProbeAllowed));
        object.insert(QStringLiteral("resolverUnknownProbeKnown"), static_cast<qint64>(op.resolverUnknownProbeKnown));
        object.insert(QStringLiteral("resolverKnownProbeUnknown"), static_cast<qint64>(op.resolverKnownProbeUnknown));
        operations.insert(it.key(), object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("elapsedMs"), elapsedMs);
    root.insert(QStringLiteral("scanned"), static_cast<qint64>(stats.scanned));
    root.insert(QStringLiteral("files"), static_cast<qint64>(stats.files));
    root.insert(QStringLiteral("directories"), static_cast<qint64>(stats.directories));
    root.insert(QStringLiteral("others"), static_cast<qint64>(stats.others));
    root.insert(QStringLiteral("reparsePoints"), static_cast<qint64>(stats.reparsePoints));
    root.insert(QStringLiteral("enumerationErrors"), static_cast<qint64>(stats.enumerationErrors));
    root.insert(QStringLiteral("hardConflictRows"), static_cast<qint64>(stats.hardConflicts));
    root.insert(QStringLiteral("reviewRows"), static_cast<qint64>(stats.reviewRows));
    root.insert(QStringLiteral("operations"), operations);
    return root;
}

void printUsage(QTextStream &out)
{
    out << "access_audit [--root PATH] [--limit N] [--out FILE] [--conflicts FILE] [--summary FILE]\n"
        << "\n"
        << "Defaults:\n"
        << "  --root C:\\\n"
        << "  --limit 50000\n"
        << "  --out .qa-tmp/access-audit.jsonl\n"
        << "  --conflicts .qa-tmp/access-audit-conflicts.jsonl\n"
        << "  --summary .qa-tmp/access-audit-summary.json\n"
        << "\n"
        << "Use --conflicts-only to keep the full output file empty and write only review rows plus summary.\n";
}

Options parseOptions(const QStringList &arguments, bool *ok)
{
    Options options;
    *ok = true;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString arg = arguments.at(i);
        auto takeValue = [&](QString *value) {
            if (i + 1 >= arguments.size()) {
                *ok = false;
                return;
            }
            *value = arguments.at(++i);
        };

        if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            *ok = false;
            return options;
        }
        if (arg == QStringLiteral("--descend-reparse-points")) {
            options.descendReparsePoints = true;
            continue;
        }
        if (arg == QStringLiteral("--conflicts-only")) {
            options.writeAllRows = false;
            continue;
        }
        if (arg == QStringLiteral("--root")) {
            takeValue(&options.root);
            continue;
        }
        if (arg == QStringLiteral("--out")) {
            takeValue(&options.outputPath);
            continue;
        }
        if (arg == QStringLiteral("--conflicts")) {
            takeValue(&options.conflictsPath);
            continue;
        }
        if (arg == QStringLiteral("--summary")) {
            takeValue(&options.summaryPath);
            continue;
        }
        if (arg == QStringLiteral("--limit") && i + 1 < arguments.size()) {
            options.limit = arguments.at(++i).toLongLong(ok);
            continue;
        }
        if (arg == QStringLiteral("--progress-every") && i + 1 < arguments.size()) {
            options.progressEvery = arguments.at(++i).toLongLong(ok);
            continue;
        }
        *ok = false;
        return options;
    }
    options.root = normalizedPath(options.root);
    if (options.limit <= 0) {
        *ok = false;
    }
    return options;
}

bool enumerateDirectory(const QString &directoryPath,
                        const Options &options,
                        QStack<QString> *pendingDirectories,
                        QList<QString> *leafPaths,
                        QTextStream &err,
                        ScanStats *stats)
{
    WIN32_FIND_DATAW data{};
    const QString pattern = searchPattern(directoryPath);
    HANDLE handle = FindFirstFileExW(pattern.toStdWString().c_str(),
                                     FindExInfoBasic,
                                     &data,
                                     FindExSearchNameMatch,
                                     nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        ++stats->enumerationErrors;
        err << "enumeration failed: " << nativePath(directoryPath)
            << " error=" << GetLastError() << " " << winErrorName(GetLastError()) << "\n";
        return false;
    }

    do {
        const QString name = QString::fromWCharArray(data.cFileName);
        if (name == QLatin1String(".") || name == QLatin1String("..")) {
            continue;
        }
        const QString childPath = QDir(directoryPath).filePath(name);
        const bool isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isReparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (isDirectory && (!isReparse || options.descendReparsePoints)) {
            pendingDirectories->push(childPath);
        } else {
            leafPaths->append(childPath);
        }
    } while (FindNextFileW(handle, &data));

    const DWORD error = GetLastError();
    FindClose(handle);
    if (error != ERROR_NO_MORE_FILES) {
        ++stats->enumerationErrors;
        err << "enumeration interrupted: " << nativePath(directoryPath)
            << " error=" << error << " " << winErrorName(error) << "\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    bool ok = true;
    const Options options = parseOptions(app.arguments(), &ok);
    if (!ok) {
        printUsage(err);
        return 2;
    }

    if (!ensureParentDirectory(options.outputPath)
        || !ensureParentDirectory(options.conflictsPath)
        || !ensureParentDirectory(options.summaryPath)) {
        err << "failed to create output directories\n";
        return 1;
    }

    QFile output(options.outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        err << "failed to open output: " << options.outputPath << "\n";
        return 1;
    }
    QFile conflicts(options.conflictsPath);
    if (!conflicts.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        err << "failed to open conflicts output: " << options.conflictsPath << "\n";
        return 1;
    }

    ScanStats stats;
    QElapsedTimer timer;
    timer.start();

    QStack<QString> pendingDirectories;
    pendingDirectories.push(options.root);

    while (!pendingDirectories.isEmpty() && stats.scanned < options.limit) {
        const QString directoryPath = pendingDirectories.pop();

        bool rowConflict = false;
        bool rowReview = false;
        const QJsonObject row = auditPath(directoryPath, &stats, &rowConflict, &rowReview);
        if (options.writeAllRows) {
            writeJsonLine(output, row);
        }
        if (rowReview) {
            writeJsonLine(conflicts, row);
        }

        if (stats.scanned >= options.limit || !QFileInfo(directoryPath).isDir()) {
            break;
        }

        QList<QString> leafPaths;
        enumerateDirectory(directoryPath, options, &pendingDirectories, &leafPaths, err, &stats);

        for (const QString &childPath : std::as_const(leafPaths)) {
            if (stats.scanned >= options.limit) {
                break;
            }
            bool childConflict = false;
            bool childReview = false;
            const QJsonObject childRow = auditPath(childPath, &stats, &childConflict, &childReview);
            if (options.writeAllRows) {
                writeJsonLine(output, childRow);
            }
            if (childReview) {
                writeJsonLine(conflicts, childRow);
            }
        }

        if (options.progressEvery > 0 && stats.scanned % options.progressEvery == 0) {
            out << "scanned=" << stats.scanned
                << " conflicts=" << stats.hardConflicts
                << " review=" << stats.reviewRows
                << " elapsedMs=" << timer.elapsed() << "\n";
            out.flush();
        }
    }

    const qint64 elapsedMs = timer.elapsed();
    QFile summary(options.summaryPath);
    if (summary.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        summary.write(QJsonDocument(statsJson(stats, elapsedMs)).toJson(QJsonDocument::Indented));
    }

    out << "done scanned=" << stats.scanned
        << " files=" << stats.files
        << " dirs=" << stats.directories
        << " conflicts=" << stats.hardConflicts
        << " review=" << stats.reviewRows
        << " enumErrors=" << stats.enumerationErrors
        << " elapsedMs=" << elapsedMs << "\n"
        << "output=" << nativePath(options.outputPath) << "\n"
        << "conflicts=" << nativePath(options.conflictsPath) << "\n"
        << "summary=" << nativePath(options.summaryPath) << "\n";
    return 0;
}
