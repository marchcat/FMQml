#pragma once

#include <QJsonObject>
#include <QString>

class LinuxAdminBroker final
{
public:
    static constexpr int CurrentProtocolVersion = 1;

    enum class BackendMode {
        Unavailable,
        Fake
    };

    enum class Operation {
        CopyFile,
        MakeDirectory,
        AtomicReplace
    };

    struct Request {
        int protocolVersion = CurrentProtocolVersion;
        QString operationId;
        QString sessionNonce;
        Operation operation = Operation::CopyFile;
        QString sourcePath;
        QString destinationPath;
        bool overwrite = false;
        bool preserveMetadata = false;
    };

    struct Result {
        bool success = false;
        QString errorCode;
        QString errorMessage;
        QString failedPath;
    };

    bool available() const;
    QString backendName() const;
    BackendMode backendMode() const;
    void setBackendModeForTesting(BackendMode mode);

    Result submitBlocking(const Request &request) const;
    static QJsonObject requestToJson(const Request &request);
    static Result requestFromJson(const QJsonObject &object, Request *request);

private:
    static QString operationToString(Operation operation);
    static bool operationFromString(const QString &value, Operation *operation);

    Result validateRequest(const Request &request) const;
    Result submitFake(const Request &request) const;

    BackendMode m_backendMode = BackendMode::Unavailable;
};
