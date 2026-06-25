#include "LinuxAdminSession.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

int fail(const QString &message)
{
    QTextStream(stderr) << message << '\n';
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    LinuxAdminSession session;
    if (session.state() != LinuxAdminSession::State::Unavailable || session.backendAvailable()) {
        return fail(QStringLiteral("session should start unavailable"));
    }

    session.setBackendAvailable(true);
    if (session.state() != LinuxAdminSession::State::Locked) {
        return fail(QStringLiteral("available backend should lock session"));
    }

    session.setTimeoutMinutes(2);
    session.beginUnlock();
    if (session.state() != LinuxAdminSession::State::Unlocking) {
        return fail(QStringLiteral("beginUnlock did not enter Unlocking"));
    }

    session.activate(1000);
    if (session.state() != LinuxAdminSession::State::Active || session.remainingSeconds(1000) != 120) {
        return fail(QStringLiteral("activate did not start a two-minute session"));
    }

    session.updateForNow(61000);
    if (session.state() != LinuxAdminSession::State::ExpiringSoon || session.remainingSeconds(61000) != 60) {
        return fail(QStringLiteral("session should be expiring soon at one minute remaining"));
    }

    session.refreshAfterOperation(70000);
    if (session.state() != LinuxAdminSession::State::Active || session.remainingSeconds(70000) != 120) {
        return fail(QStringLiteral("refreshAfterOperation did not reset idle timeout"));
    }

    session.updateForNow(190001);
    if (session.state() != LinuxAdminSession::State::Expired || session.remainingSeconds(190001) != 0) {
        return fail(QStringLiteral("session did not expire"));
    }

    session.lock();
    if (session.state() != LinuxAdminSession::State::Locked) {
        return fail(QStringLiteral("manual lock should return to Locked"));
    }

    session.setBackendAvailable(false);
    if (session.state() != LinuxAdminSession::State::Unavailable || session.remainingSeconds(70000) != 0) {
        return fail(QStringLiteral("unavailable backend should clear session"));
    }

    session.setTimeoutMinutes(0);
    if (session.timeoutMinutes() != 1) {
        return fail(QStringLiteral("timeout lower bound was not enforced"));
    }

    session.setTimeoutMinutes(1000);
    if (session.timeoutMinutes() != 120) {
        return fail(QStringLiteral("timeout upper bound was not enforced"));
    }

    return 0;
}
