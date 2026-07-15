#pragma once

#include "../core/BatchRenameEngine.h"
#include "../models/BatchRenamePreviewModel.h"
#include "../models/BatchRenameRuleModel.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

class BatchRenameSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(BatchRenameRuleModel *ruleModel READ ruleModel CONSTANT)
    Q_PROPERTY(BatchRenamePreviewModel *previewModel READ previewModel CONSTANT)
    Q_PROPERTY(QStringList sourcePaths READ sourcePaths WRITE setSourcePaths NOTIFY sourcePathsChanged)
    Q_PROPERTY(int selectedRuleIndex READ selectedRuleIndex WRITE setSelectedRuleIndex NOTIFY selectedRuleIndexChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(bool hasConflicts READ hasConflicts NOTIFY summaryChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY summaryChanged)
    Q_PROPERTY(int changedCount READ changedCount NOTIFY summaryChanged)
    Q_PROPERTY(bool isApplied READ isApplied NOTIFY summaryChanged)
    Q_PROPERTY(int successCount READ successCount NOTIFY summaryChanged)
    Q_PROPERTY(int failCount READ failCount NOTIFY summaryChanged)

public:
    explicit BatchRenameSession(QObject *parent = nullptr);

    BatchRenameRuleModel *ruleModel();
    BatchRenamePreviewModel *previewModel();
    QStringList sourcePaths() const;
    void setSourcePaths(const QStringList &paths);
    int selectedRuleIndex() const;
    void setSelectedRuleIndex(int index);
    QString filterText() const;
    void setFilterText(const QString &text);
    bool hasConflicts() const;
    int totalCount() const;
    int changedCount() const;
    bool isApplied() const;
    int successCount() const;
    int failCount() const;

    Q_INVOKABLE void reset(const QStringList &paths = {});
    Q_INVOKABLE int addRule(const QString &type);
    Q_INVOKABLE bool removeSelectedRule();
    Q_INVOKABLE bool updateSelectedRule(const QVariantMap &values);
    Q_INVOKABLE QVariantList engineRules() const;
    Q_INVOKABLE void regeneratePreview();
    Q_INVOKABLE void applyResults(const QVariantList &results);
    void flushPendingPreviewForTest();

signals:
    void sourcePathsChanged();
    void selectedRuleIndexChanged();
    void filterTextChanged();
    void summaryChanged();
    void previewGenerated();

private:
    void schedulePreview();

    BatchRenameEngine m_engine;
    BatchRenameRuleModel m_ruleModel;
    BatchRenamePreviewModel m_previewModel;
    QTimer m_previewTimer;
    QStringList m_sourcePaths;
    QString m_filterText;
    int m_selectedRuleIndex = 0;
    bool m_isApplied = false;
};
