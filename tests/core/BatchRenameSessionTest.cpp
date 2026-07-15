#include "BatchRenameSession.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <cstdio>

namespace {
bool expect(bool condition, const char *message)
{
    if (!condition) std::fprintf(stderr, "%s\n", message);
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    BatchRenameSession session;
    int previewGenerationCount = 0;
    QObject::connect(&session, &BatchRenameSession::previewGenerated,
                     [&previewGenerationCount]() { ++previewGenerationCount; });

    if (!expect(session.ruleModel()->rowCount() == 1, "Session must start with one default rule")) return 1;
    if (!expect(!session.ruleModel()->removeRule(0) && !session.ruleModel()->setRule(-1, {}),
                "Invalid edits and removal of the only rule must be rejected")) return 1;
    session.ruleModel()->addRule(QStringLiteral("format"));
    session.ruleModel()->setRule(0, {{QStringLiteral("search"), QStringLiteral("old")},
                                     {QStringLiteral("replace"), QStringLiteral("new")}});
    session.ruleModel()->setRule(1, {{QStringLiteral("prefix"), QStringLiteral("prefix-")}});
    if (!expect(session.ruleModel()->moveRule(1, 0)
                    && session.ruleModel()->get(0).value(QStringLiteral("type")).toString() == QStringLiteral("format"),
                "Moving a rule must change typed rule order")) return 1;
    session.reset();
    session.setSourcePaths({QStringLiteral("/tmp/One File.txt"), QStringLiteral("/tmp/Two File.txt")});
    if (!expect(session.totalCount() == 2, "Sources must create two preview rows")) return 1;

    session.updateSelectedRule({{QStringLiteral("type"), QStringLiteral("transform")},
                                {QStringLiteral("mode"), QStringLiteral("spaces-dash")}});
    session.updateSelectedRule({{QStringLiteral("mode"), QStringLiteral("uppercase")}});
    session.updateSelectedRule({{QStringLiteral("mode"), QStringLiteral("spaces-dash")}});
    previewGenerationCount = 0;
    session.flushPendingPreviewForTest();
    if (!expect(previewGenerationCount == 1, "A burst of rule edits must coalesce into one preview")) return 1;
    const QModelIndex first = session.previewModel()->index(0);
    if (!expect(session.previewModel()->data(first, BatchRenamePreviewModel::NewNameRole).toString()
                    == QStringLiteral("One-File.txt"),
                "Typed transform rule must preserve engine behavior")) return 1;

    const int secondRule = session.addRule(QStringLiteral("format"));
    session.updateSelectedRule({{QStringLiteral("prefix"), QStringLiteral("new-")},
                                {QStringLiteral("suffixText"), QStringLiteral("-done")}});
    session.flushPendingPreviewForTest();
    if (!expect(secondRule == 1 && session.ruleModel()->rowCount() == 2,
                "Adding a rule must select and append it")) return 1;
    if (!expect(session.previewModel()->data(first, BatchRenamePreviewModel::NewNameRole).toString()
                    == QStringLiteral("new-One-File-done.txt"),
                "Stacked rules must retain their current order")) return 1;

    session.setFilterText(QStringLiteral("two-file"));
    if (!expect(session.previewModel()->rowCount() == 1 && session.totalCount() == 2,
                "Filtering must not change complete preview counts")) return 1;
    session.setFilterText({});

    session.reset({QStringLiteral("/tmp/A.txt"), QStringLiteral("/tmp/B.txt")});
    session.updateSelectedRule({{QStringLiteral("type"), QStringLiteral("replace")},
                                {QStringLiteral("search"), QStringLiteral("A|B")},
                                {QStringLiteral("replace"), QStringLiteral("same")},
                                {QStringLiteral("caseSensitive"), true},
                                {QStringLiteral("regex"), true}});
    session.flushPendingPreviewForTest();
    if (!expect(session.hasConflicts(), "Duplicate destination must block the session")) return 1;
    session.setFilterText(QStringLiteral("A.txt"));
    if (!expect(session.hasConflicts(), "Filtering must not hide global conflicts")) return 1;

    session.reset({QStringLiteral("/tmp/One File.txt"), QStringLiteral("/tmp/Two File.txt")});
    session.updateSelectedRule({{QStringLiteral("type"), QStringLiteral("transform")},
                                {QStringLiteral("mode"), QStringLiteral("spaces-dash")}});
    session.flushPendingPreviewForTest();

    QVariantList reversedResults;
    reversedResults.append(QVariantMap{{QStringLiteral("oldPath"), QStringLiteral("/tmp/Two File.txt")},
                                       {QStringLiteral("success"), false},
                                       {QStringLiteral("error"), QStringLiteral("failed two")}});
    reversedResults.append(QVariantMap{{QStringLiteral("oldPath"), QStringLiteral("/tmp/One File.txt")},
                                       {QStringLiteral("success"), true},
                                       {QStringLiteral("error"), QString()}});
    session.applyResults(reversedResults);
    if (!expect(session.isApplied() && session.successCount() == 1 && session.failCount() == 1,
                "Apply counts must use path-matched results")) return 1;
    if (!expect(session.previewModel()->data(session.previewModel()->index(1), BatchRenamePreviewModel::ErrorRole).toString()
                    == QStringLiteral("failed two"),
                "Out-of-order results must reconcile by oldPath")) return 1;

    session.reset({QStringLiteral("/tmp/reset.txt")});
    if (!expect(!session.isApplied() && session.ruleModel()->rowCount() == 1 && session.totalCount() == 1,
                "Reset must clear stale apply and rule state")) return 1;

    QStringList manyPaths;
    manyPaths.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
        manyPaths.append(QStringLiteral("/tmp/batch-item-%1.txt").arg(i, 4, 10, QLatin1Char('0')));
    }
    session.reset(manyPaths);
    session.updateSelectedRule({{QStringLiteral("type"), QStringLiteral("format")},
                                {QStringLiteral("prefix"), QStringLiteral("renamed-")}});
    session.flushPendingPreviewForTest();
    if (!expect(session.totalCount() == 5000 && !session.hasConflicts(),
                "5000-path preview must be complete and deterministic")) return 1;
    if (!expect(session.previewModel()->data(session.previewModel()->index(4999), BatchRenamePreviewModel::NewNameRole).toString()
                    == QStringLiteral("renamed-batch-item-4999.txt"),
                "Large preview must preserve deterministic row naming")) return 1;
    return 0;
}
