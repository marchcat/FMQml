#include "FtpFileProviderPlugin.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <optional>

#include <QAbstractSocket>
#include <QBuffer>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFuture>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QTcpSocket>
#include <QtConcurrent>
#include <QtGlobal>
#include <QTime>
#include <QUrl>
#include <QVector>

namespace {

constexpr int DefaultFtpPort = 21;
constexpr int ControlTimeoutMs = 15000;
constexpr int DataConnectTimeoutMs = 15000;
constexpr int TransferIdleTimeoutMs = 30000;
constexpr QLatin1String AuthRequiredError{"FTP auth required / dev in progress"};

struct FtpUrl {
    bool valid = false;
    bool hasCredentials = false;
    QString host;
    int port = DefaultFtpPort;
    QString path = QStringLiteral("/");
    QString normalized;
    QString error;
};

bool isFtpSchemePath(const QString &path)
{
    const QString trimmed = path.trimmed();
    const int separatorIndex = trimmed.indexOf(QStringLiteral("://"));
    if (separatorIndex <= 0) {
        return false;
    }
    return trimmed.left(separatorIndex).compare(QStringLiteral("ftp"), Qt::CaseInsensitive) == 0;
}

QString cleanFtpPath(QString path)
{
    path = path.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (path.isEmpty()) {
        return QStringLiteral("/");
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    path = QDir::cleanPath(path);
    if (path == QLatin1String(".")) {
        return QStringLiteral("/");
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    return path;
}

QString buildFtpUrl(const QString &host, int port, const QString &path, const QString &userName = {}, const QString &password = {})
{
    QUrl url;
    url.setScheme(QStringLiteral("ftp"));
    url.setHost(host.toLower());
    if (port > 0 && port != DefaultFtpPort) {
        url.setPort(port);
    }
    if (!userName.isEmpty() || !password.isEmpty()) {
        url.setUserName(userName, QUrl::DecodedMode);
        url.setPassword(password, QUrl::DecodedMode);
    }
    url.setPath(cleanFtpPath(path), QUrl::DecodedMode);
    return url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::DecodeReserved);
}

FtpUrl parseFtpUrl(const QString &path)
{
    FtpUrl result;
    const QString trimmed = path.trimmed();
    if (!isFtpSchemePath(trimmed)) {
        result.error = QStringLiteral("FTP URL must start with ftp://");
        return result;
    }

    const QUrl url(trimmed, QUrl::TolerantMode);
    if (url.scheme().compare(QStringLiteral("ftp"), Qt::CaseInsensitive) != 0) {
        result.error = QStringLiteral("FTP URL must start with ftp://");
        return result;
    }

    result.host = url.host().trimmed().toLower();
    if (result.host.isEmpty()) {
        result.error = QStringLiteral("FTP host is required");
        return result;
    }

    const int parsedPort = url.port(DefaultFtpPort);
    if (parsedPort <= 0 || parsedPort > 65535) {
        result.error = QStringLiteral("FTP port is invalid");
        return result;
    }
    result.port = parsedPort;

    const int authorityStart = trimmed.indexOf(QStringLiteral("://")) + 3;
    const int authorityEnd = trimmed.indexOf(QLatin1Char('/'), authorityStart);
    const QString authority = authorityEnd < 0
        ? trimmed.mid(authorityStart)
        : trimmed.mid(authorityStart, authorityEnd - authorityStart);
    result.hasCredentials = authority.contains(QLatin1Char('@'))
        || !url.userName().isEmpty()
        || !url.password().isEmpty();

    result.path = cleanFtpPath(url.path(QUrl::FullyDecoded));
    result.normalized = buildFtpUrl(result.host,
                                    result.port,
                                    result.path,
                                    result.hasCredentials ? url.userName(QUrl::FullyDecoded) : QString{},
                                    result.hasCredentials ? url.password(QUrl::FullyDecoded) : QString{});
    result.valid = true;
    return result;
}

bool ftpTraceEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("FM_FTP_TRACE")
        || !QString::fromLocal8Bit(qgetenv("FM_FTP_TRACE_FILE")).trimmed().isEmpty();
    return enabled;
}

QString ftpTraceFilePath()
{
    static const QString path = QString::fromLocal8Bit(qgetenv("FM_FTP_TRACE_FILE")).trimmed();
    return path;
}

QMutex &ftpTraceFileMutex()
{
    static QMutex mutex;
    return mutex;
}

QString traceUrlFor(const FtpUrl &url)
{
    if (!url.host.isEmpty()) {
        return buildFtpUrl(url.host, url.port, url.path);
    }
    return url.normalized.isEmpty() ? QStringLiteral("<invalid>") : url.normalized;
}

QString socketStateName(QAbstractSocket::SocketState state)
{
    switch (state) {
    case QAbstractSocket::UnconnectedState:
        return QStringLiteral("unconnected");
    case QAbstractSocket::HostLookupState:
        return QStringLiteral("host-lookup");
    case QAbstractSocket::ConnectingState:
        return QStringLiteral("connecting");
    case QAbstractSocket::ConnectedState:
        return QStringLiteral("connected");
    case QAbstractSocket::BoundState:
        return QStringLiteral("bound");
    case QAbstractSocket::ClosingState:
        return QStringLiteral("closing");
    case QAbstractSocket::ListeningState:
        return QStringLiteral("listening");
    }
    return QStringLiteral("unknown");
}

QString safeCommandForTrace(QString command)
{
    command = command.trimmed();
    if (command.startsWith(QStringLiteral("PASS "), Qt::CaseInsensitive)) {
        return QStringLiteral("PASS <redacted>");
    }
    return command;
}

void traceFtp(const QString &stage, const FtpUrl &url, const QString &detail = {})
{
    if (!ftpTraceEnabled()) {
        return;
    }

    const QString line = detail.isEmpty()
        ? QStringLiteral("[FTP] %1 url=%2").arg(stage, traceUrlFor(url))
        : QStringLiteral("[FTP] %1 url=%2 %3").arg(stage, traceUrlFor(url), detail);
    if (detail.isEmpty()) {
        qInfo().noquote() << "[FTP]" << stage << "url=" << traceUrlFor(url);
    } else {
        qInfo().noquote() << "[FTP]" << stage << "url=" << traceUrlFor(url) << detail;
    }

    const QString traceFile = ftpTraceFilePath();
    if (traceFile.isEmpty()) {
        return;
    }

    QMutexLocker locker(&ftpTraceFileMutex());
    QFile file(traceFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << QLatin1Char(' ')
           << line
           << QLatin1Char('\n');
}

QString suffixForName(const QString &name)
{
    const int dotIndex = name.lastIndexOf(QLatin1Char('.'));
    if (dotIndex <= 0 || dotIndex == name.size() - 1) {
        return {};
    }
    return name.mid(dotIndex + 1).toLower();
}

bool isImageSuffix(const QString &suffix)
{
    static const QStringList imageSuffixes = {
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("png"),
        QStringLiteral("gif"),
        QStringLiteral("bmp"),
        QStringLiteral("webp"),
        QStringLiteral("ico"),
        QStringLiteral("svg"),
        QStringLiteral("svgz")
    };
    return imageSuffixes.contains(suffix.toLower());
}

QString formatSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kib = double(bytes) / 1024.0;
    if (kib < 1024.0) {
        return QStringLiteral("%1 KB").arg(kib, 0, 'f', 1);
    }
    const double mib = kib / 1024.0;
    if (mib < 1024.0) {
        return QStringLiteral("%1 MB").arg(mib, 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(mib / 1024.0, 0, 'f', 1);
}

QString fileNameForFtpPath(const QString &path)
{
    const FtpUrl url = parseFtpUrl(path);
    if (!url.valid) {
        return {};
    }
    if (url.path == QLatin1String("/")) {
        return url.host;
    }
    const QStringList parts = url.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? url.host : parts.constLast();
}

QString parentFtpPath(const QString &path)
{
    const FtpUrl url = parseFtpUrl(path);
    if (!url.valid || url.path == QLatin1String("/")) {
        return {};
    }

    QString parent = url.path;
    const int slashIndex = parent.lastIndexOf(QLatin1Char('/'));
    parent = slashIndex <= 0 ? QStringLiteral("/") : parent.left(slashIndex);
    return buildFtpUrl(url.host, url.port, parent);
}

QString childFtpPath(const QString &parentPath, const QString &name)
{
    const FtpUrl url = parseFtpUrl(parentPath);
    if (!url.valid) {
        return {};
    }

    QString cleanName = name.trimmed();
    cleanName.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList parts = cleanName.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return url.normalized;
    }

    QString child = url.path;
    for (const QString &part : parts) {
        if (!child.endsWith(QLatin1Char('/'))) {
            child += QLatin1Char('/');
        }
        child += part;
    }
    return buildFtpUrl(url.host, url.port, child);
}

int monthNumber(const QString &month)
{
    static const QStringList months = {
        QStringLiteral("jan"),
        QStringLiteral("feb"),
        QStringLiteral("mar"),
        QStringLiteral("apr"),
        QStringLiteral("may"),
        QStringLiteral("jun"),
        QStringLiteral("jul"),
        QStringLiteral("aug"),
        QStringLiteral("sep"),
        QStringLiteral("oct"),
        QStringLiteral("nov"),
        QStringLiteral("dec")
    };
    const int index = months.indexOf(month.left(3).toLower());
    return index < 0 ? 0 : index + 1;
}

QDateTime unixListDateTime(const QString &month, int day, const QString &yearOrTime)
{
    const int monthValue = monthNumber(month);
    if (monthValue <= 0 || day <= 0) {
        return {};
    }

    QDate date;
    QTime time;
    if (yearOrTime.contains(QLatin1Char(':'))) {
        date = QDate(QDate::currentDate().year(), monthValue, day);
        time = QTime::fromString(yearOrTime, QStringLiteral("H:mm"));
        const QDateTime candidate(date, time.isValid() ? time : QTime(0, 0));
        if (candidate > QDateTime::currentDateTime().addDays(1)) {
            date = date.addYears(-1);
        }
    } else {
        bool ok = false;
        const int year = yearOrTime.toInt(&ok);
        if (!ok) {
            return {};
        }
        date = QDate(year, monthValue, day);
        time = QTime(0, 0);
    }

    if (!date.isValid()) {
        return {};
    }
    return QDateTime(date, time.isValid() ? time : QTime(0, 0));
}

QDateTime dosListDateTime(const QString &dateText, const QString &timeText, const QString &ampm)
{
    const QStringList dateParts = dateText.split(QLatin1Char('-'));
    if (dateParts.size() != 3) {
        return {};
    }

    bool okMonth = false;
    bool okDay = false;
    bool okYear = false;
    const int month = dateParts.at(0).toInt(&okMonth);
    const int day = dateParts.at(1).toInt(&okDay);
    int year = dateParts.at(2).toInt(&okYear);
    if (!okMonth || !okDay || !okYear) {
        return {};
    }
    if (year < 100) {
        year += year >= 70 ? 1900 : 2000;
    }

    QTime time = QTime::fromString(timeText, QStringLiteral("H:mm"));
    if (!time.isValid()) {
        return {};
    }
    const QString ampmLower = ampm.toLower();
    if (ampmLower == QLatin1String("pm") && time.hour() < 12) {
        time = time.addSecs(12 * 60 * 60);
    } else if (ampmLower == QLatin1String("am") && time.hour() == 12) {
        time = time.addSecs(-12 * 60 * 60);
    }

    const QDate date(year, month, day);
    return date.isValid() ? QDateTime(date, time) : QDateTime{};
}

FileEntry ftpEntry(const QString &parentPath,
                   const QString &name,
                   bool isDirectory,
                   qint64 size,
                   const QDateTime &modified = {})
{
    FileEntry entry;
    entry.name = name;
    entry.path = childFtpPath(parentPath, name);
    entry.suffix = isDirectory ? QString{} : suffixForName(name);
    entry.size = isDirectory ? 0 : size;
    entry.sizeText = isDirectory ? QString{} : formatSize(entry.size);
    entry.modified = modified;
    entry.created = modified;
    if (modified.isValid()) {
        const QLocale locale;
        entry.modifiedText = locale.toString(modified, QLocale::ShortFormat);
        entry.createdText = entry.modifiedText;
    }
    entry.attributesText = isDirectory ? QStringLiteral("DR") : QStringLiteral("R");
    entry.isDirectory = isDirectory;
    entry.isHidden = name.startsWith(QLatin1Char('.'));
    entry.isImage = !isDirectory && isImageSuffix(entry.suffix);
    entry.hasThumbnail = entry.isImage;
    entry.isReadOnly = true;
    return entry;
}

FileEntry ftpRootEntry(const FtpUrl &url)
{
    FileEntry entry;
    entry.name = url.host;
    entry.path = url.normalized;
    entry.attributesText = QStringLiteral("DR");
    entry.isDirectory = true;
    entry.isReadOnly = true;
    return entry;
}

std::optional<FileEntry> parseUnixListLine(const QString &line, const QString &parentPath)
{
    static const QRegularExpression re(
        QStringLiteral("^([bcdlps-])[rwxStTs-]{9}\\s+\\d+\\s+\\S+\\s+\\S+\\s+(\\d+)\\s+(\\S{3})\\s+(\\d{1,2})\\s+(\\S+)\\s+(.+)$"));
    const QRegularExpressionMatch match = re.match(line);
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    QString name = match.captured(6);
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return std::nullopt;
    }
    if (match.captured(1) == QLatin1String("l")) {
        const int linkIndex = name.indexOf(QStringLiteral(" -> "));
        if (linkIndex > 0) {
            name = name.left(linkIndex);
        }
    }

    bool okSize = false;
    const qint64 size = match.captured(2).toLongLong(&okSize);
    bool okDay = false;
    const int day = match.captured(4).toInt(&okDay);
    const bool isDirectory = match.captured(1) == QLatin1String("d");
    return ftpEntry(parentPath,
                    name,
                    isDirectory,
                    okSize ? size : 0,
                    okDay ? unixListDateTime(match.captured(3), day, match.captured(5)) : QDateTime{});
}

std::optional<FileEntry> parseDosListLine(const QString &line, const QString &parentPath)
{
    static const QRegularExpression re(
        QStringLiteral("^(\\d{2}-\\d{2}-\\d{2,4})\\s+(\\d{1,2}:\\d{2})(AM|PM)\\s+(<DIR>|\\d+)\\s+(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(line);
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    const QString name = match.captured(5);
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return std::nullopt;
    }

    const bool isDirectory = match.captured(4).compare(QStringLiteral("<DIR>"), Qt::CaseInsensitive) == 0;
    bool okSize = false;
    const qint64 size = isDirectory ? 0 : match.captured(4).toLongLong(&okSize);
    return ftpEntry(parentPath,
                    name,
                    isDirectory,
                    okSize ? size : 0,
                    dosListDateTime(match.captured(1), match.captured(2), match.captured(3)));
}

QList<FileEntry> parseListData(const QByteArray &data, const QString &parentPath, bool includeHidden)
{
    QList<FileEntry> entries;
    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
    entries.reserve(lines.size());

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("total "))) {
            continue;
        }

        std::optional<FileEntry> entry = parseUnixListLine(line, parentPath);
        if (!entry) {
            entry = parseDosListLine(line, parentPath);
        }
        if (!entry || (!includeHidden && entry->isHidden)) {
            continue;
        }
        entries.append(*entry);
    }

    std::sort(entries.begin(), entries.end(), [](const FileEntry &left, const FileEntry &right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory && !right.isDirectory;
        }
        return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
    });
    return entries;
}

QString commandPath(const QString &path)
{
    return cleanFtpPath(path);
}

QString replyText(const QStringList &lines)
{
    QStringList cleaned;
    cleaned.reserve(lines.size());
    for (QString line : lines) {
        if (line.size() >= 4 && line.at(0).isDigit() && line.at(1).isDigit() && line.at(2).isDigit()) {
            line = line.mid(4);
        }
        if (!line.trimmed().isEmpty()) {
            cleaned.append(line.trimmed());
        }
    }
    return cleaned.join(QStringLiteral(" "));
}

QString ftpReplyError(int code, const QStringList &lines, const QString &fallback)
{
    const QString text = replyText(lines);
    if (text.isEmpty()) {
        return fallback;
    }
    return QStringLiteral("FTP %1: %2").arg(code).arg(text);
}

class FtpClient
{
public:
    FtpClient(FtpUrl url, std::function<bool()> shouldCancel)
        : m_url(std::move(url))
        , m_shouldCancel(std::move(shouldCancel))
    {
    }

    QList<FileEntry> list(bool includeHidden, QString *error)
    {
        traceFtp(QStringLiteral("list-begin"), m_url,
                 QStringLiteral("includeHidden=%1").arg(includeHidden));
        if (!connectAndLogin(error)) {
            traceFtp(QStringLiteral("list-failed"), m_url, error ? *error : QString{});
            return {};
        }

        QList<FileEntry> entries;
        if (!listOnConnection(m_url.path, m_url.normalized, includeHidden, &entries, error)) {
            traceFtp(QStringLiteral("list-failed"), m_url, error ? *error : QString{});
            return {};
        }
        traceFtp(QStringLiteral("list-ok"), m_url,
                 QStringLiteral("entries=%1").arg(entries.size()));
        return entries;
    }

    std::optional<FileEntry> stat(QString *error)
    {
        traceFtp(QStringLiteral("stat-begin"), m_url);
        if (!m_url.valid) {
            setError(error, m_url.error);
            traceFtp(QStringLiteral("stat-failed"), m_url, m_url.error);
            return std::nullopt;
        }
        if (m_url.hasCredentials) {
            setError(error, QString(AuthRequiredError));
            traceFtp(QStringLiteral("stat-failed"), m_url, QString(AuthRequiredError));
            return std::nullopt;
        }
        if (m_url.path == QLatin1String("/")) {
            traceFtp(QStringLiteral("stat-ok"), m_url, QStringLiteral("type=directory root=true"));
            return ftpRootEntry(m_url);
        }
        if (!connectAndLogin(error)) {
            traceFtp(QStringLiteral("stat-failed"), m_url, error ? *error : QString{});
            return std::nullopt;
        }

        int code = 0;
        QStringList lines;
        if (sendCommand(QStringLiteral("CWD %1").arg(commandPath(m_url.path)), {250}, &code, &lines, nullptr)) {
            traceFtp(QStringLiteral("stat-ok"), m_url, QStringLiteral("type=directory"));
            return ftpEntry(parentFtpPath(m_url.normalized), fileNameForFtpPath(m_url.normalized), true, 0);
        }

        if (sendCommand(QStringLiteral("TYPE I"), {200}, nullptr, nullptr, nullptr)) {
            if (sendCommand(QStringLiteral("SIZE %1").arg(commandPath(m_url.path)), {213}, &code, &lines, nullptr)) {
                bool ok = false;
                const qint64 size = replyText(lines).toLongLong(&ok);
                QDateTime modified;
                int mdtmCode = 0;
                QStringList mdtmLines;
                if (sendCommand(QStringLiteral("MDTM %1").arg(commandPath(m_url.path)), {213}, &mdtmCode, &mdtmLines, nullptr)) {
                    modified = QDateTime::fromString(replyText(mdtmLines), QStringLiteral("yyyyMMddhhmmss"));
                }
                return ftpEntry(parentFtpPath(m_url.normalized),
                                fileNameForFtpPath(m_url.normalized),
                                false,
                                ok ? size : 0,
                                modified);
            }
        }

        const QString parent = parentFtpPath(m_url.normalized);
        if (parent.isEmpty()) {
            setError(error, QStringLiteral("FTP path does not exist"));
            return std::nullopt;
        }
        const FtpUrl parentUrl = parseFtpUrl(parent);
        QList<FileEntry> entries;
        if (listOnConnection(parentUrl.path, parentUrl.normalized, true, &entries, error)) {
            const QString name = fileNameForFtpPath(m_url.normalized);
            for (const FileEntry &entry : entries) {
                if (entry.name == name) {
                    traceFtp(QStringLiteral("stat-ok"), m_url,
                             QStringLiteral("type=%1 via=list").arg(entry.isDirectory ? QStringLiteral("directory") : QStringLiteral("file")));
                    return entry;
                }
            }
        }

        setError(error, QStringLiteral("FTP path does not exist"));
        traceFtp(QStringLiteral("stat-failed"), m_url, QStringLiteral("FTP path does not exist"));
        return std::nullopt;
    }

    bool isDirectory(QString *error)
    {
        if (!m_url.valid) {
            setError(error, m_url.error);
            return false;
        }
        if (m_url.hasCredentials) {
            setError(error, QString(AuthRequiredError));
            return false;
        }
        if (m_url.path == QLatin1String("/")) {
            return true;
        }
        if (!connectAndLogin(error)) {
            return false;
        }
        return sendCommand(QStringLiteral("CWD %1").arg(commandPath(m_url.path)), {250}, nullptr, nullptr, error);
    }

    QByteArray download(QString *error)
    {
        traceFtp(QStringLiteral("download-begin"), m_url);
        if (!connectAndLogin(error)) {
            traceFtp(QStringLiteral("download-failed"), m_url, error ? *error : QString{});
            return {};
        }

        if (!sendCommand(QStringLiteral("TYPE I"), {200}, nullptr, nullptr, error)) {
            traceFtp(QStringLiteral("download-failed"), m_url, error ? *error : QString{});
            return {};
        }

        QTcpSocket dataSocket;
        if (!openPassiveDataSocket(dataSocket, error)) {
            traceFtp(QStringLiteral("download-failed"), m_url, error ? *error : QString{});
            return {};
        }

        int code = 0;
        if (!sendCommand(QStringLiteral("RETR %1").arg(commandPath(m_url.path)), {125, 150}, &code, nullptr, error)) {
            dataSocket.abort();
            traceFtp(QStringLiteral("download-failed"), m_url, error ? *error : QString{});
            return {};
        }

        QByteArray data;
        if (!readDataSocket(dataSocket, &data, error)) {
            traceFtp(QStringLiteral("download-failed"), m_url, error ? *error : QString{});
            return {};
        }

        if (!readExpectedReply({226, 250}, error)) {
            traceFtp(QStringLiteral("download-failed"), m_url, error ? *error : QString{});
            return {};
        }
        traceFtp(QStringLiteral("download-ok"), m_url,
                 QStringLiteral("bytes=%1").arg(data.size()));
        return data;
    }

private:
    static void setError(QString *target, const QString &message)
    {
        if (target) {
            *target = message;
        }
    }

    bool cancelled() const
    {
        return m_shouldCancel && m_shouldCancel();
    }

    bool waitForConnected(QTcpSocket &socket,
                          int timeoutMs,
                          const QString &stage,
                          const QString &detail,
                          QString *error)
    {
        QElapsedTimer timer;
        timer.start();
        traceFtp(stage + QStringLiteral("-begin"), m_url,
                 QStringLiteral("%1 state=%2 timeoutMs=%3")
                     .arg(detail, socketStateName(socket.state()))
                     .arg(timeoutMs));
        while (socket.state() != QAbstractSocket::ConnectedState) {
            if (cancelled()) {
                setError(error, QStringLiteral("FTP operation cancelled"));
                socket.abort();
                traceFtp(stage + QStringLiteral("-cancelled"), m_url,
                         QStringLiteral("%1 elapsedMs=%2 state=%3")
                             .arg(detail)
                             .arg(timer.elapsed())
                             .arg(socketStateName(socket.state())));
                return false;
            }
            const int remaining = timeoutMs - int(timer.elapsed());
            if (remaining <= 0) {
                setError(error, QStringLiteral("FTP connection failed: %1").arg(socket.errorString()));
                socket.abort();
                traceFtp(stage + QStringLiteral("-timeout"), m_url,
                         QStringLiteral("%1 elapsedMs=%2 state=%3 socketError=%4")
                             .arg(detail)
                             .arg(timer.elapsed())
                             .arg(socketStateName(socket.state()), socket.errorString()));
                return false;
            }
            if (socket.waitForConnected(qMin(250, remaining))) {
                break;
            }
            if (socket.state() == QAbstractSocket::UnconnectedState) {
                setError(error, QStringLiteral("FTP connection failed: %1").arg(socket.errorString()));
                socket.abort();
                traceFtp(stage + QStringLiteral("-failed"), m_url,
                         QStringLiteral("%1 elapsedMs=%2 state=%3 socketError=%4")
                             .arg(detail)
                             .arg(timer.elapsed())
                             .arg(socketStateName(socket.state()), socket.errorString()));
                return false;
            }
        }
        traceFtp(stage + QStringLiteral("-ok"), m_url,
                 QStringLiteral("%1 elapsedMs=%2 state=%3")
                     .arg(detail)
                     .arg(timer.elapsed())
                     .arg(socketStateName(socket.state())));
        return true;
    }

    bool waitForBytesWritten(QTcpSocket &socket, QString *error)
    {
        QElapsedTimer timer;
        timer.start();
        while (socket.bytesToWrite() > 0) {
            if (cancelled()) {
                setError(error, QStringLiteral("FTP operation cancelled"));
                socket.abort();
                return false;
            }
            const int remaining = ControlTimeoutMs - int(timer.elapsed());
            if (remaining <= 0) {
                setError(error, QStringLiteral("FTP write failed: %1").arg(socket.errorString()));
                socket.abort();
                return false;
            }
            if (socket.waitForBytesWritten(qMin(250, remaining))) {
                continue;
            }
            if (socket.state() == QAbstractSocket::UnconnectedState) {
                setError(error, QStringLiteral("FTP write failed: %1").arg(socket.errorString()));
                socket.abort();
                return false;
            }
        }
        return true;
    }

    bool readReply(int *code, QStringList *lines, QString *error)
    {
        QStringList collected;
        int replyCode = -1;
        bool multiLine = false;
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < ControlTimeoutMs) {
            if (cancelled()) {
                setError(error, QStringLiteral("FTP operation cancelled"));
                m_control.abort();
                return false;
            }

            while (m_control.canReadLine()) {
                const QString line = QString::fromUtf8(m_control.readLine()).trimmed();
                if (line.isEmpty()) {
                    continue;
                }
                collected.append(line);

                if (line.size() < 3
                    || !line.at(0).isDigit()
                    || !line.at(1).isDigit()
                    || !line.at(2).isDigit()) {
                    continue;
                }

                bool ok = false;
                const int lineCode = line.left(3).toInt(&ok);
                if (!ok) {
                    continue;
                }

                if (replyCode < 0) {
                    replyCode = lineCode;
                    multiLine = line.size() > 3 && line.at(3) == QLatin1Char('-');
                    if (!multiLine) {
                        if (code) {
                            *code = replyCode;
                        }
                        if (lines) {
                            *lines = collected;
                        }
                        return true;
                    }
                } else if (multiLine
                           && lineCode == replyCode
                           && line.size() > 3
                           && line.at(3) == QLatin1Char(' ')) {
                    if (code) {
                        *code = replyCode;
                    }
                    if (lines) {
                        *lines = collected;
                    }
                    return true;
                }
            }

            if (!m_control.waitForReadyRead(250) && m_control.state() == QAbstractSocket::UnconnectedState) {
                break;
            }
        }

        setError(error, QStringLiteral("FTP reply timed out"));
        if (lines) {
            *lines = collected;
        }
        return false;
    }

    bool sendCommand(const QString &command,
                     std::initializer_list<int> expected,
                     int *code,
                     QStringList *lines,
                     QString *error)
    {
        traceFtp(QStringLiteral("command"), m_url, safeCommandForTrace(command));
        const QByteArray bytes = command.toUtf8() + "\r\n";
        if (m_control.write(bytes) != bytes.size() || !waitForBytesWritten(m_control, error)) {
            traceFtp(QStringLiteral("command-write-failed"), m_url,
                     safeCommandForTrace(command) + QStringLiteral(" error=") + (error ? *error : QString{}));
            return false;
        }

        int replyCode = 0;
        QStringList replyLines;
        if (!readReply(&replyCode, &replyLines, error)) {
            traceFtp(QStringLiteral("reply-failed"), m_url,
                     safeCommandForTrace(command) + QStringLiteral(" error=") + (error ? *error : QString{}));
            return false;
        }

        if (code) {
            *code = replyCode;
        }
        if (lines) {
            *lines = replyLines;
        }

        if (std::find(expected.begin(), expected.end(), replyCode) == expected.end()) {
            setError(error, ftpReplyError(replyCode, replyLines, QStringLiteral("FTP command failed")));
            traceFtp(QStringLiteral("command-unexpected-reply"), m_url,
                     QStringLiteral("%1 code=%2 text=%3")
                         .arg(safeCommandForTrace(command))
                         .arg(replyCode)
                         .arg(replyText(replyLines)));
            return false;
        }
        traceFtp(QStringLiteral("reply"), m_url,
                 QStringLiteral("%1 code=%2 text=%3")
                     .arg(safeCommandForTrace(command))
                     .arg(replyCode)
                     .arg(replyText(replyLines)));
        return true;
    }

    bool readExpectedReply(std::initializer_list<int> expected, QString *error)
    {
        int code = 0;
        QStringList lines;
        if (!readReply(&code, &lines, error)) {
            return false;
        }
        if (std::find(expected.begin(), expected.end(), code) == expected.end()) {
            setError(error, ftpReplyError(code, lines, QStringLiteral("FTP transfer failed")));
            traceFtp(QStringLiteral("reply-unexpected"), m_url,
                     QStringLiteral("code=%1 text=%2").arg(code).arg(replyText(lines)));
            return false;
        }
        traceFtp(QStringLiteral("reply"), m_url,
                 QStringLiteral("code=%1 text=%2").arg(code).arg(replyText(lines)));
        return true;
    }

    bool connectAndLogin(QString *error)
    {
        if (!m_url.valid) {
            setError(error, m_url.error);
            return false;
        }
        if (m_url.hasCredentials) {
            setError(error, QString(AuthRequiredError));
            return false;
        }
        if (m_loggedIn) {
            return true;
        }

        traceFtp(QStringLiteral("control-connect-setup"), m_url,
                 QStringLiteral("host=%1 port=%2").arg(m_url.host).arg(m_url.port));
        m_control.connectToHost(m_url.host, quint16(m_url.port));
        if (!waitForConnected(m_control,
                              ControlTimeoutMs,
                              QStringLiteral("control-connect"),
                              QStringLiteral("host=%1 port=%2").arg(m_url.host).arg(m_url.port),
                              error)) {
            return false;
        }

        if (!readExpectedReply({220}, error)) {
            traceFtp(QStringLiteral("banner-failed"), m_url, error ? *error : QString{});
            return false;
        }
        traceFtp(QStringLiteral("banner-ok"), m_url);

        int code = 0;
        if (!sendCommand(QStringLiteral("USER anonymous"), {230, 331}, &code, nullptr, error)) {
            return false;
        }
        if (code == 331
            && !sendCommand(QStringLiteral("PASS anonymous@"), {230}, nullptr, nullptr, error)) {
            return false;
        }

        m_loggedIn = true;
        traceFtp(QStringLiteral("login-ok"), m_url);
        return true;
    }

    bool openPassiveDataSocket(QTcpSocket &dataSocket, QString *error)
    {
        traceFtp(QStringLiteral("pasv-begin"), m_url);
        int code = 0;
        QStringList lines;
        if (!sendCommand(QStringLiteral("PASV"), {227}, &code, &lines, error)) {
            return false;
        }

        static const QRegularExpression re(QStringLiteral("\\((\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)\\)"));
        const QRegularExpressionMatch match = re.match(lines.join(QStringLiteral(" ")));
        if (!match.hasMatch()) {
            setError(error, QStringLiteral("FTP server returned an invalid PASV address"));
            return false;
        }

        QStringList hostParts;
        hostParts.reserve(4);
        for (int i = 1; i <= 4; ++i) {
            hostParts.append(match.captured(i));
        }
        bool okHigh = false;
        bool okLow = false;
        const int high = match.captured(5).toInt(&okHigh);
        const int low = match.captured(6).toInt(&okLow);
        if (!okHigh || !okLow) {
            setError(error, QStringLiteral("FTP server returned an invalid PASV port"));
            return false;
        }

        const QString passiveHost = hostParts.join(QLatin1Char('.'));
        const quint16 passivePort = quint16(high * 256 + low);
        traceFtp(QStringLiteral("pasv-target"), m_url,
                 QStringLiteral("host=%1 port=%2").arg(passiveHost).arg(passivePort));
        dataSocket.connectToHost(passiveHost, passivePort);
        if (waitForConnected(dataSocket,
                             DataConnectTimeoutMs,
                             QStringLiteral("data-connect"),
                             QStringLiteral("host=%1 port=%2 source=pasv").arg(passiveHost).arg(passivePort),
                             nullptr)) {
            return true;
        }

        if (passiveHost != m_url.host) {
            traceFtp(QStringLiteral("data-connect-retry"), m_url,
                     QStringLiteral("host=%1 port=%2 source=control-host").arg(m_url.host).arg(passivePort));
            dataSocket.abort();
            dataSocket.connectToHost(m_url.host, passivePort);
            if (waitForConnected(dataSocket,
                                 DataConnectTimeoutMs,
                                 QStringLiteral("data-connect"),
                                 QStringLiteral("host=%1 port=%2 source=control-host").arg(m_url.host).arg(passivePort),
                                 error)) {
                return true;
            }
        }

        setError(error, QStringLiteral("FTP data connection failed: %1").arg(dataSocket.errorString()));
        traceFtp(QStringLiteral("data-connect-failed"), m_url,
                 QStringLiteral("socketError=%1").arg(dataSocket.errorString()));
        return false;
    }

    bool readDataSocket(QTcpSocket &dataSocket, QByteArray *data, QString *error)
    {
        QElapsedTimer idleTimer;
        idleTimer.start();

        while (true) {
            if (cancelled()) {
                setError(error, QStringLiteral("FTP operation cancelled"));
                dataSocket.abort();
                return false;
            }

            if (dataSocket.bytesAvailable() > 0) {
                if (data) {
                    data->append(dataSocket.readAll());
                } else {
                    dataSocket.readAll();
                }
                idleTimer.restart();
                continue;
            }

            if (dataSocket.state() == QAbstractSocket::UnconnectedState) {
                break;
            }

            if (dataSocket.waitForReadyRead(250)) {
                continue;
            }
            if (dataSocket.state() == QAbstractSocket::UnconnectedState) {
                if (dataSocket.bytesAvailable() > 0 && data) {
                    data->append(dataSocket.readAll());
                }
                break;
            }
            if (idleTimer.elapsed() > TransferIdleTimeoutMs) {
                setError(error, QStringLiteral("FTP transfer timed out"));
                dataSocket.abort();
                return false;
            }
        }
        return true;
    }

    bool listOnConnection(const QString &serverPath,
                          const QString &parentPath,
                          bool includeHidden,
                          QList<FileEntry> *entries,
                          QString *error)
    {
        traceFtp(QStringLiteral("list-command-begin"), m_url,
                 QStringLiteral("path=%1").arg(serverPath));
        if (!sendCommand(QStringLiteral("TYPE A"), {200}, nullptr, nullptr, error)) {
            return false;
        }

        QTcpSocket dataSocket;
        if (!openPassiveDataSocket(dataSocket, error)) {
            return false;
        }

        const QString command = serverPath == QLatin1String("/")
            ? QStringLiteral("LIST")
            : QStringLiteral("LIST %1").arg(commandPath(serverPath));
        int code = 0;
        if (!sendCommand(command, {125, 150, 226}, &code, nullptr, error)) {
            dataSocket.abort();
            return false;
        }

        QByteArray data;
        if (code != 226 && !readDataSocket(dataSocket, &data, error)) {
            traceFtp(QStringLiteral("list-data-failed"), m_url, error ? *error : QString{});
            return false;
        }
        if (code != 226 && !readExpectedReply({226, 250}, error)) {
            traceFtp(QStringLiteral("list-complete-failed"), m_url, error ? *error : QString{});
            return false;
        }

        if (entries) {
            *entries = parseListData(data, parentPath, includeHidden);
            traceFtp(QStringLiteral("list-command-ok"), m_url,
                     QStringLiteral("bytes=%1 entries=%2").arg(data.size()).arg(entries->size()));
        } else {
            traceFtp(QStringLiteral("list-command-ok"), m_url,
                     QStringLiteral("bytes=%1").arg(data.size()));
        }
        return true;
    }

    FtpUrl m_url;
    std::function<bool()> m_shouldCancel;
    QTcpSocket m_control;
    bool m_loggedIn = false;
};

class FtpFileProvider final : public FileProvider
{
public:
    explicit FtpFileProvider(QObject *parent = nullptr)
        : FileProvider(parent)
    {
    }

    ~FtpFileProvider() override
    {
        cancel();
        for (QFuture<void> &future : m_scanFutures) {
            future.waitForFinished();
        }
    }

    QString scheme() const override { return QStringLiteral("ftp"); }
    bool canHandle(const QString &path) const override { return isFtpSchemePath(path); }
    Capabilities capabilities() const override { return Browse | ReadMetadata | Transfer; }

    void scan(const QString &path) override
    {
        cancel();
        pruneFinishedScans();

        const FtpUrl url = parseFtpUrl(path);
        const QString scanPath = url.valid ? url.normalized : path.trimmed();
        m_currentPath = scanPath;
        const int generation = m_generation.fetch_add(1) + 1;
        emit started();
        traceFtp(QStringLiteral("scan-begin"), url,
                 QStringLiteral("generation=%1 path=%2").arg(generation).arg(scanPath));

        const bool includeHidden = m_showHidden;
        m_scanFutures.append(QtConcurrent::run([this, url, scanPath, includeHidden, generation]() {
            if (!url.valid) {
                traceFtp(QStringLiteral("scan-failed"), url, url.error);
                emit finished(scanPath, false, generation, url.error);
                return;
            }
            if (url.hasCredentials) {
                traceFtp(QStringLiteral("scan-failed"), url, QString(AuthRequiredError));
                emit finished(scanPath, false, generation, QString(AuthRequiredError));
                return;
            }

            QString error;
            FtpClient client(url, [this, generation]() {
                return generation != m_generation.load();
            });
            const QList<FileEntry> entries = client.list(includeHidden, &error);
            if (generation != m_generation.load()) {
                traceFtp(QStringLiteral("scan-cancelled"), url,
                         QStringLiteral("generation=%1 currentGeneration=%2").arg(generation).arg(m_generation.load()));
                return;
            }
            if (!error.isEmpty()) {
                traceFtp(QStringLiteral("scan-failed"), url, error);
                emit finished(scanPath, false, generation, error);
                return;
            }
            if (!entries.isEmpty()) {
                emit batchReady(entries, generation);
            }
            traceFtp(QStringLiteral("scan-ok"), url,
                     QStringLiteral("generation=%1 entries=%2").arg(generation).arg(entries.size()));
            emit finished(scanPath, true, generation, {});
        }));
    }

    void cancel() override
    {
        m_generation.fetch_add(1);
    }

    void setShowHidden(bool show) override { m_showHidden = show; }
    bool isRunning() const override
    {
        return std::any_of(m_scanFutures.cbegin(), m_scanFutures.cend(), [](const QFuture<void> &future) {
            return !future.isFinished();
        });
    }
    QString currentPath() const override { return m_currentPath; }
    int currentGeneration() const override { return m_generation.load(); }

    bool pathExists(const QString &path) const override
    {
        return entryInfo(path).has_value();
    }

    bool isDirectory(const QString &path) const override
    {
        clearLastError();
        const FtpUrl url = parseFtpUrl(path);
        QString error;
        FtpClient client(url, []() { return false; });
        const bool result = client.isDirectory(&error);
        if (!result && !error.isEmpty()) {
            setLastError(error);
        }
        return result;
    }

    bool isSymLink(const QString &) const override { return false; }
    QString normalizedPath(const QString &path) const override
    {
        const FtpUrl url = parseFtpUrl(path);
        return url.valid ? url.normalized : path.trimmed();
    }
    QString fileName(const QString &path) const override { return fileNameForFtpPath(path); }
    QString absolutePath(const QString &path) const override { return normalizedPath(path); }
    QString parentPath(const QString &path) const override { return parentFtpPath(path); }
    QString childPath(const QString &parentPath, const QString &name) const override { return childFtpPath(parentPath, name); }

    std::optional<FileEntry> entryInfo(const QString &path) const override
    {
        clearLastError();
        const FtpUrl url = parseFtpUrl(path);
        QString error;
        FtpClient client(url, []() { return false; });
        std::optional<FileEntry> entry = client.stat(&error);
        if (!entry && !error.isEmpty()) {
            setLastError(error);
        }
        return entry;
    }

    bool ensureParentDirectory(const QString &) const override { return failReadOnly(); }
    bool makePath(const QString &) const override { return failReadOnly(); }
    bool removePath(const QString &) const override { return failReadOnly(); }

    QStringList childPaths(const QString &path, bool includeHidden = true) const override
    {
        clearLastError();
        const FtpUrl url = parseFtpUrl(path);
        QString error;
        FtpClient client(url, []() { return false; });
        const QList<FileEntry> entries = client.list(includeHidden, &error);
        if (!error.isEmpty()) {
            setLastError(error);
            return {};
        }

        QStringList paths;
        paths.reserve(entries.size());
        for (const FileEntry &entry : entries) {
            paths.append(entry.path);
        }
        return paths;
    }

    bool movePath(const QString &, const QString &) const override { return failReadOnly(); }

    std::unique_ptr<QIODevice> openRead(const QString &path) const override
    {
        clearLastError();
        const FtpUrl url = parseFtpUrl(path);
        QString error;
        FtpClient client(url, []() { return false; });
        const QByteArray data = client.download(&error);
        if (!error.isEmpty()) {
            setLastError(error);
            return nullptr;
        }

        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(data);
        if (!buffer->open(QIODevice::ReadOnly)) {
            setLastError(QStringLiteral("Cannot open FTP download buffer"));
            return nullptr;
        }
        return buffer;
    }

    std::unique_ptr<QIODevice> openWrite(const QString &, bool truncate = true) const override
    {
        Q_UNUSED(truncate)
        failReadOnly();
        return nullptr;
    }

    bool renamePath(const QString &, const QString &) override { return failReadOnly(); }
    bool createFolder(const QString &, const QString &, QString *createdPath = nullptr) override
    {
        if (createdPath) {
            createdPath->clear();
        }
        return failReadOnly();
    }
    bool createFile(const QString &, const QString &, QString *createdPath = nullptr) override
    {
        if (createdPath) {
            createdPath->clear();
        }
        return failReadOnly();
    }

    QString lastErrorString() const override
    {
        QMutexLocker locker(&m_errorMutex);
        return m_lastError;
    }

    void clearLastError() const override
    {
        QMutexLocker locker(&m_errorMutex);
        m_lastError.clear();
    }

private:
    void pruneFinishedScans()
    {
        for (qsizetype i = m_scanFutures.size() - 1; i >= 0; --i) {
            if (m_scanFutures.at(i).isFinished()) {
                m_scanFutures.removeAt(i);
            }
        }
    }

    bool failReadOnly() const
    {
        setLastError(QStringLiteral("ftp:// is read-only"));
        return false;
    }

    void setLastError(const QString &error) const
    {
        QMutexLocker locker(&m_errorMutex);
        m_lastError = error;
    }

    QString m_currentPath = QStringLiteral("ftp://");
    QVector<QFuture<void>> m_scanFutures;
    std::atomic<int> m_generation{0};
    bool m_showHidden = false;
    mutable QMutex m_errorMutex;
    mutable QString m_lastError;
};

} // namespace

int FtpFileProviderPlugin::apiVersion() const
{
    return FM_FILE_PROVIDER_PLUGIN_API_VERSION;
}

QString FtpFileProviderPlugin::pluginId() const
{
    return QStringLiteral("fm.ftp-provider");
}

QString FtpFileProviderPlugin::displayName() const
{
    return QStringLiteral("FTP Provider");
}

QStringList FtpFileProviderPlugin::schemes() const
{
    return {QStringLiteral("ftp")};
}

bool FtpFileProviderPlugin::canHandle(const QString &path) const
{
    return isFtpSchemePath(path);
}

std::unique_ptr<FileProvider> FtpFileProviderPlugin::createProvider()
{
    return std::make_unique<FtpFileProvider>();
}
