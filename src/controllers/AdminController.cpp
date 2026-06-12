#include "AdminController.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QStringList>

#ifdef Q_OS_WIN
#include <QDir>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
#ifdef Q_OS_WIN
QString quoteWindowsArgument(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("\"\"");
    }

    QString escaped = arg;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    if (escaped.contains(QLatin1Char(' ')) || escaped.contains(QLatin1Char('\t'))) {
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return escaped;
}
#endif
}

AdminController::AdminController(QObject *parent)
    : QObject(parent)
    , m_isElevated(detectElevated())
{
}

bool AdminController::isElevated() const
{
    return m_isElevated;
}

bool AdminController::canRelaunchAsAdmin() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool AdminController::relaunchAsAdmin()
{
#ifdef Q_OS_WIN
    if (m_isElevated) {
        return true;
    }

    const QString executablePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) {
        arguments.removeFirst();
    }

    QStringList quotedArguments;
    quotedArguments.reserve(arguments.size());
    for (const QString &argument : std::as_const(arguments)) {
        quotedArguments.append(quoteWindowsArgument(argument));
    }
    const QString parameters = quotedArguments.join(QLatin1Char(' '));

    const std::wstring executable = executablePath.toStdWString();
    const std::wstring params = parameters.toStdWString();

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.hwnd = nullptr;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = params.empty() ? nullptr : params.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        return false;
    }

    QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    return true;
#else
    return false;
#endif
}

void AdminController::refresh()
{
    const bool elevated = detectElevated();
    if (m_isElevated == elevated) {
        return;
    }
    m_isElevated = elevated;
    emit isElevatedChanged();
}

bool AdminController::detectElevated() const
{
#ifdef Q_OS_WIN
    BOOL isMember = FALSE;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    if (!AllocateAndInitializeSid(&authority, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0,
                                  &adminGroup)) {
        return false;
    }

    const BOOL ok = CheckTokenMembership(nullptr, adminGroup, &isMember);
    FreeSid(adminGroup);
    return ok && isMember;
#else
    return false;
#endif
}
