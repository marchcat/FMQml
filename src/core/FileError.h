#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace FileError {

enum class Code {
    None,
    AccessDenied,
    InUse,
    AlreadyExists,
    InvalidName,
    DiskFull,
    PathNotFound,
    DriveNotReady,
    ReadOnly,
    NetworkUnavailable,
    AuthRequired,
    QuotaExceeded,
    RateLimited,
    UnsupportedOperation,
    Unknown
};

QString codeToString(Code code);
QString titleForCode(Code code);
QVariantMap make(Code code,
                 const QString &path,
                 const QString &operation,
                 const QString &message,
                 const QStringList &actions = {});
QVariantMap classify(const QString &message,
                     const QString &path = {},
                     const QString &operation = {});

} // namespace FileError
