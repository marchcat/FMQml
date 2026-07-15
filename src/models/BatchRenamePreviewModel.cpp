#include "BatchRenamePreviewModel.h"

#include <QHash>

BatchRenamePreviewModel::BatchRenamePreviewModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BatchRenamePreviewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleRows.size();
}

QVariant BatchRenamePreviewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) return {};
    const Row &row = m_rows.at(m_visibleRows.at(index.row()));
    switch (role) {
    case OldPathRole: return row.preview.oldPath;
    case OldNameRole: return row.preview.oldName;
    case NewNameRole: return row.preview.newName;
    case NewPathRole: return row.preview.newPath;
    case HasConflictRole: return row.preview.hasConflict;
    case ErrorRole: return row.applyError.isEmpty() ? row.preview.error : row.applyError;
    case SuccessRole: return row.success;
    default: return {};
    }
}

QHash<int, QByteArray> BatchRenamePreviewModel::roleNames() const
{
    return {{OldPathRole, "oldPath"}, {OldNameRole, "oldName"}, {NewNameRole, "newName"},
            {NewPathRole, "newPath"}, {HasConflictRole, "hasConflict"}, {ErrorRole, "error"},
            {SuccessRole, "success"}};
}

void BatchRenamePreviewModel::setPreview(const QList<BatchRenameEngine::RenamePreview> &preview)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(preview.size());
    for (const auto &item : preview) m_rows.append({item, false, {}});
    m_visibleRows.clear();
    const QString query = m_filterText.trimmed();
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto &item = m_rows.at(i).preview;
        if (query.isEmpty() || item.oldName.contains(query, Qt::CaseInsensitive)
            || item.newName.contains(query, Qt::CaseInsensitive)) {
            m_visibleRows.append(i);
        }
    }
    endResetModel();
    emit countChanged();
}

void BatchRenamePreviewModel::clear()
{
    setPreview({});
}

void BatchRenamePreviewModel::setFilterText(const QString &filterText)
{
    const QString normalized = filterText.trimmed();
    if (m_filterText == normalized) return;
    m_filterText = normalized;
    rebuildVisibleRows();
}

void BatchRenamePreviewModel::applyResults(const QVariantList &results)
{
    if (results.isEmpty()) {
        clear();
        return;
    }
    QHash<QString, QVariantMap> byPath;
    for (const QVariant &result : results) {
        const QVariantMap map = result.toMap();
        byPath.insert(map.value(QStringLiteral("oldPath")).toString(), map);
    }
    for (Row &row : m_rows) {
        const QVariantMap result = byPath.value(row.preview.oldPath);
        row.success = result.value(QStringLiteral("success")).toBool();
        row.applyError = result.isEmpty() ? QStringLiteral("Missing rename result")
                                          : result.value(QStringLiteral("error")).toString();
    }
    if (!m_visibleRows.isEmpty()) emit dataChanged(index(0), index(m_visibleRows.size() - 1), {ErrorRole, SuccessRole});
}

int BatchRenamePreviewModel::totalCount() const { return m_rows.size(); }
int BatchRenamePreviewModel::conflictCount() const
{
    int count = 0;
    for (const Row &row : m_rows) count += row.preview.hasConflict ? 1 : 0;
    return count;
}
int BatchRenamePreviewModel::changedCount() const
{
    int count = 0;
    for (const Row &row : m_rows) count += row.preview.oldName != row.preview.newName ? 1 : 0;
    return count;
}
int BatchRenamePreviewModel::successCount() const
{
    int count = 0;
    for (const Row &row : m_rows) count += row.success ? 1 : 0;
    return count;
}
int BatchRenamePreviewModel::failCount() const { return m_rows.size() - successCount(); }

void BatchRenamePreviewModel::rebuildVisibleRows()
{
    beginResetModel();
    m_visibleRows.clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto &item = m_rows.at(i).preview;
        if (m_filterText.isEmpty() || item.oldName.contains(m_filterText, Qt::CaseInsensitive)
            || item.newName.contains(m_filterText, Qt::CaseInsensitive)) {
            m_visibleRows.append(i);
        }
    }
    endResetModel();
    emit countChanged();
}
