#pragma once

#include <QAbstractListModel>
#include <QVariantList>

struct BatchRenameRule {
    enum class Type { Replace, Format, Numbering, Template, Transform };

    Type type = Type::Replace;
    QString search;
    QString replacement;
    bool caseSensitive = false;
    bool regex = false;
    QString prefix;
    QString suffix;
    int start = 0;
    int padding = 2;
    QString position = QStringLiteral("suffix");
    QString text;
    int sequenceStart = 1;
    int sequencePadding = 2;
    QString mode = QStringLiteral("lowercase");
};

class BatchRenameRuleModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        SearchRole,
        ReplacementRole,
        CaseSensitiveRole,
        RegexRole,
        PrefixRole,
        SuffixRole,
        StartRole,
        PaddingRole,
        PositionRole,
        TextRole,
        SequenceStartRole,
        SequencePaddingRole,
        ModeRole,
        TitleRole,
        SummaryRole
    };

    explicit BatchRenameRuleModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE int addRule(const QString &type = QStringLiteral("replace"));
    Q_INVOKABLE bool removeRule(int index);
    Q_INVOKABLE bool setRule(int index, const QVariantMap &values);
    Q_INVOKABLE bool moveRule(int from, int to);
    void resetToDefault();

    QVariantList engineRules() const;

signals:
    void countChanged();
    void rulesChanged();

private:
    static BatchRenameRule::Type typeFromString(const QString &type);
    static QString typeName(BatchRenameRule::Type type);
    static QVariantMap toMap(const BatchRenameRule &rule);
    static BatchRenameRule fromMap(const QVariantMap &values, const BatchRenameRule &fallback);
    static QString title(const BatchRenameRule &rule);
    static QString summary(const BatchRenameRule &rule);

    QList<BatchRenameRule> m_rules;
};
