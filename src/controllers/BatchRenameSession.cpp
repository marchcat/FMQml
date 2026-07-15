#include "BatchRenameSession.h"

BatchRenameSession::BatchRenameSession(QObject *parent)
    : QObject(parent), m_engine(this), m_ruleModel(this), m_previewModel(this)
{
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(120);
    connect(&m_previewTimer, &QTimer::timeout, this, &BatchRenameSession::regeneratePreview);
    connect(&m_ruleModel, &BatchRenameRuleModel::rulesChanged, this, &BatchRenameSession::schedulePreview);
}

BatchRenameRuleModel *BatchRenameSession::ruleModel() { return &m_ruleModel; }
BatchRenamePreviewModel *BatchRenameSession::previewModel() { return &m_previewModel; }
QStringList BatchRenameSession::sourcePaths() const { return m_sourcePaths; }
void BatchRenameSession::setSourcePaths(const QStringList &paths)
{
    if (m_sourcePaths == paths) return;
    m_sourcePaths = paths;
    m_isApplied = false;
    emit sourcePathsChanged();
    regeneratePreview();
}
int BatchRenameSession::selectedRuleIndex() const { return m_selectedRuleIndex; }
void BatchRenameSession::setSelectedRuleIndex(int index)
{
    if (index < 0 || index >= m_ruleModel.rowCount() || m_selectedRuleIndex == index) return;
    m_selectedRuleIndex = index;
    emit selectedRuleIndexChanged();
}
QString BatchRenameSession::filterText() const { return m_filterText; }
void BatchRenameSession::setFilterText(const QString &text)
{
    if (m_filterText == text) return;
    m_filterText = text;
    m_previewModel.setFilterText(text);
    emit filterTextChanged();
}
bool BatchRenameSession::hasConflicts() const { return m_previewModel.conflictCount() > 0; }
int BatchRenameSession::totalCount() const { return m_previewModel.totalCount(); }
int BatchRenameSession::changedCount() const { return m_previewModel.changedCount(); }
bool BatchRenameSession::isApplied() const { return m_isApplied; }
int BatchRenameSession::successCount() const { return m_previewModel.successCount(); }
int BatchRenameSession::failCount() const { return m_isApplied ? m_previewModel.failCount() : 0; }

void BatchRenameSession::reset(const QStringList &paths)
{
    m_previewTimer.stop();
    m_ruleModel.resetToDefault();
    m_sourcePaths = paths;
    m_filterText.clear();
    m_previewModel.setFilterText({});
    m_selectedRuleIndex = 0;
    m_isApplied = false;
    emit sourcePathsChanged();
    emit filterTextChanged();
    emit selectedRuleIndexChanged();
    regeneratePreview();
}

int BatchRenameSession::addRule(const QString &type)
{
    const int index = m_ruleModel.addRule(type);
    setSelectedRuleIndex(index);
    regeneratePreview();
    return index;
}

bool BatchRenameSession::removeSelectedRule()
{
    const int removed = m_selectedRuleIndex;
    if (!m_ruleModel.removeRule(removed)) return false;
    m_selectedRuleIndex = qMin(removed, m_ruleModel.rowCount() - 1);
    emit selectedRuleIndexChanged();
    regeneratePreview();
    return true;
}

bool BatchRenameSession::updateSelectedRule(const QVariantMap &values)
{
    return m_ruleModel.setRule(m_selectedRuleIndex, values);
}

QVariantList BatchRenameSession::engineRules() const { return m_ruleModel.engineRules(); }

void BatchRenameSession::regeneratePreview()
{
    m_previewTimer.stop();
    if (m_isApplied) return;
    m_previewModel.setPreview(m_engine.generatePreview(m_sourcePaths, m_ruleModel.engineRules()));
    emit summaryChanged();
    emit previewGenerated();
}

void BatchRenameSession::applyResults(const QVariantList &results)
{
    m_previewTimer.stop();
    m_previewModel.applyResults(results);
    m_isApplied = true;
    emit summaryChanged();
}

void BatchRenameSession::flushPendingPreviewForTest()
{
    if (m_previewTimer.isActive()) regeneratePreview();
}

void BatchRenameSession::schedulePreview()
{
    if (!m_isApplied) m_previewTimer.start();
}
