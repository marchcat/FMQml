#pragma once

#include <QString>

namespace MegaAuth {

QString savedSession();
QString savedEmail();
bool hasSavedAuthorization();
bool rememberAuthorization(const QString &session, const QString &email);
bool clearSavedAuthorization();

} // namespace MegaAuth
