#include "LinuxAdminBroker.h"
#include "LinuxAdminPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonValue>

namespace {

LinuxAdminBroker::Result okResult()
{
    return {true, {}, {}, {}};
}

LinuxAdminBroker::Result failResult(const QString &code, const QString &message, const QString &path = {})
{
    return {false, code, message, path};
}

QString parentPathFor(const QString &path)
{
    return QFileInfo(path).absoluteDir().absolutePath();
}

} // namespace

bool LinuxAdminBroker::available() const
{
    return m_backendMode != BackendMode::Unavailable;
}

QString LinuxAdminBroker::backendName() const
{
    switch (m_backendMode) {
    case BackendMode::Fake:
        return QStringLiteral("fake");
    case BackendMode::Unavailable:
        break;
    }
    return QStringLiteral("unavailable");
}

LinuxAdminBroker::BackendMode LinuxAdminBroker::backendMode() const
{
    return m_backendMode;
}

void LinuxAdminBroker::setBackendModeForTesting(BackendMode mode)
{
    m_backendMode = mode;
}

LinuxAdminBroker::Result LinuxAdminBroker::submitBlocking(const Request &request) const
{
    const Result validation = validateRequest(request);
    if (!validation.success) {
        return validation;
    }

    switch (m_backendMode) {
    case BackendMode::Fake:
        return submitFake(request);
    case BackendMode::Unavailable:
        break;
    }
    return failResult(QStringLiteral("backend-unavailable"), QStringLiteral("Linux admin backend is unavailable"));
}

QJsonObject LinuxAdminBroker::requestToJson(const Request &request)
{
    QJsonObject object;
    object.insert(QStringLiteral("protocolVersion"), request.protocolVersion);
    object.insert(QStringLiteral("operationId"), request.operationId);
    object.insert(QStringLiteral("sessionNonce"), request.sessionNonce);
    object.insert(QStringLiteral("operation"), operationToString(request.operation));
    object.insert(QStringLiteral("sourcePath"), request.sourcePath);
    object.insert(QStringLiteral("destinationPath"), request.destinationPath);
    object.insert(QStringLiteral("overwrite"), request.overwrite);
    object.insert(QStringLiteral("preserveMetadata"), request.preserveMetadata);
    return object;
}

LinuxAdminBroker::Result LinuxAdminBroker::requestFromJson(const QJsonObject &object, Request *request)
{
    if (!request) {
        return failResult(QStringLiteral("invalid-request"), QStringLiteral("Request output is null"));
    }

    const QJsonValue versionValue = object.value(QStringLiteral("protocolVersion"));
    if (!versionValue.isDouble()) {
        return failResult(QStringLiteral("protocol-mismatch"), QStringLiteral("Protocol version is missing"));
    }
    const int protocolVersion = versionValue.toInt();
    if (protocolVersion != CurrentProtocolVersion) {
        return failResult(QStringLiteral("protocol-mismatch"), QStringLiteral("Unsupported admin helper protocol version"));
    }

    Operation operation = Operation::CopyFile;
    if (!operationFromString(object.value(QStringLiteral("operation")).toString(), &operation)) {
        return failResult(QStringLiteral("invalid-operation"), QStringLiteral("Invalid operation"));
    }

    Request parsed;
    parsed.protocolVersion = protocolVersion;
    parsed.operationId = object.value(QStringLiteral("operationId")).toString();
    parsed.sessionNonce = object.value(QStringLiteral("sessionNonce")).toString();
    parsed.operation = operation;
    parsed.sourcePath = object.value(QStringLiteral("sourcePath")).toString();
    parsed.destinationPath = object.value(QStringLiteral("destinationPath")).toString();
    parsed.overwrite = object.value(QStringLiteral("overwrite")).toBool(false);
    parsed.preserveMetadata = object.value(QStringLiteral("preserveMetadata")).toBool(false);
    *request = parsed;
    return okResult();
}

QString LinuxAdminBroker::operationToString(Operation operation)
{
    switch (operation) {
    case Operation::CopyFile:
        return QStringLiteral("copyFile");
    case Operation::MakeDirectory:
        return QStringLiteral("makeDirectory");
    case Operation::AtomicReplace:
        return QStringLiteral("atomicReplace");
    }
    return {};
}

bool LinuxAdminBroker::operationFromString(const QString &value, Operation *operation)
{
    if (!operation) {
        return false;
    }
    if (value == QLatin1String("copyFile")) {
        *operation = Operation::CopyFile;
        return true;
    }
    if (value == QLatin1String("makeDirectory")) {
        *operation = Operation::MakeDirectory;
        return true;
    }
    if (value == QLatin1String("atomicReplace")) {
        *operation = Operation::AtomicReplace;
        return true;
    }
    return false;
}

LinuxAdminPolicy::Operation policyOperationFor(LinuxAdminBroker::Operation operation)
{
    switch (operation) {
    case LinuxAdminBroker::Operation::CopyFile:
        return LinuxAdminPolicy::Operation::CopyFile;
    case LinuxAdminBroker::Operation::MakeDirectory:
        return LinuxAdminPolicy::Operation::MakeDirectory;
    case LinuxAdminBroker::Operation::AtomicReplace:
        return LinuxAdminPolicy::Operation::AtomicReplace;
    }
    return LinuxAdminPolicy::Operation::CopyFile;
}

LinuxAdminBroker::Result LinuxAdminBroker::validateRequest(const Request &request) const
{
    if (request.protocolVersion != CurrentProtocolVersion) {
        return failResult(QStringLiteral("protocol-mismatch"), QStringLiteral("Unsupported admin helper protocol version"));
    }
    if (request.operationId.trimmed().isEmpty()) {
        return failResult(QStringLiteral("invalid-request"), QStringLiteral("Operation id is empty"));
    }
    if (request.sessionNonce.trimmed().isEmpty()) {
        return failResult(QStringLiteral("invalid-request"), QStringLiteral("Session nonce is empty"));
    }

    const LinuxAdminPolicy::Decision policy = LinuxAdminPolicy::validate(
        policyOperationFor(request.operation),
        request.sourcePath,
        request.destinationPath);
    if (!policy.allowed) {
        return failResult(policy.errorCode, policy.errorMessage, policy.failedPath);
    }

    return okResult();
}

LinuxAdminBroker::Result LinuxAdminBroker::submitFake(const Request &request) const
{
    const QString destination = QDir::cleanPath(request.destinationPath);

    switch (request.operation) {
    case Operation::MakeDirectory:
        if (!QDir().mkpath(destination)) {
            return failResult(QStringLiteral("mkdir-failed"), QStringLiteral("Failed to create destination directory"), destination);
        }
        return okResult();

    case Operation::CopyFile: {
        const QString parentPath = parentPathFor(destination);
        if (!QFileInfo(parentPath).isDir()) {
            return failResult(QStringLiteral("parent-missing"), QStringLiteral("Destination parent directory is missing"), parentPath);
        }
        if (QFileInfo::exists(destination)) {
            if (!request.overwrite) {
                return failResult(QStringLiteral("destination-exists"), QStringLiteral("Destination already exists"), destination);
            }
            if (!QFile::remove(destination)) {
                return failResult(QStringLiteral("remove-failed"), QStringLiteral("Failed to remove existing destination"), destination);
            }
        }
        if (!QFile::copy(request.sourcePath, destination)) {
            return failResult(QStringLiteral("copy-failed"), QStringLiteral("Failed to copy file"), destination);
        }
        return okResult();
    }

    case Operation::AtomicReplace: {
        const QString parentPath = parentPathFor(destination);
        if (!QFileInfo(parentPath).isDir()) {
            return failResult(QStringLiteral("parent-missing"), QStringLiteral("Destination parent directory is missing"), parentPath);
        }
        if (QFileInfo::exists(destination) && !request.overwrite) {
            return failResult(QStringLiteral("destination-exists"), QStringLiteral("Destination already exists"), destination);
        }

        const QString partPath = destination + QStringLiteral(".fm-admin-replace-part");
        QFile::remove(partPath);
        if (!QFile::copy(request.sourcePath, partPath)) {
            return failResult(QStringLiteral("copy-failed"), QStringLiteral("Failed to copy replacement file"), partPath);
        }
        if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
            QFile::remove(partPath);
            return failResult(QStringLiteral("remove-failed"), QStringLiteral("Failed to remove existing destination"), destination);
        }
        if (!QFile::rename(partPath, destination)) {
            QFile::remove(partPath);
            return failResult(QStringLiteral("rename-failed"), QStringLiteral("Failed to install replacement file"), destination);
        }
        return okResult();
    }
    }

    return failResult(QStringLiteral("invalid-operation"), QStringLiteral("Invalid operation"));
}
