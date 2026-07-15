#pragma once

#include "../core/BatchRenameEngine.h"

#include <QAbstractListModel>

class BatchRenamePreviewModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    struct Row {
        BatchRenameEngine::RenamePreview preview;
        bool success = false;
        QString applyError;
    };

    enum Role {
        OldPathRole = Qt::UserRole + 1,
        OldNameRole,
        NewNameRole,
        NewPathRole,
        HasConflictRole,
        ErrorRole,
        SuccessRole
    };

    explicit BatchRenamePreviewModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPreview(const QList<BatchRenameEngine::RenamePreview> &preview);
    void clear();
    void setFilterText(const QString &filterText);
    void applyResults(const QVariantList &results);
    int totalCount() const;
    int conflictCount() const;
    int changedCount() const;
    int successCount() const;
    int failCount() const;

signals:
    void countChanged();

private:
    void rebuildVisibleRows();

    QList<Row> m_rows;
    QList<int> m_visibleRows;
    QString m_filterText;
};
