#pragma once

#include <QObject>

class AdminController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isElevated READ isElevated NOTIFY isElevatedChanged)
    Q_PROPERTY(bool canRelaunchAsAdmin READ canRelaunchAsAdmin CONSTANT)

public:
    explicit AdminController(QObject *parent = nullptr);

    bool isElevated() const;
    bool canRelaunchAsAdmin() const;

    Q_INVOKABLE bool relaunchAsAdmin();
    Q_INVOKABLE void refresh();

signals:
    void isElevatedChanged();

private:
    bool detectElevated() const;

    bool m_isElevated = false;
};
