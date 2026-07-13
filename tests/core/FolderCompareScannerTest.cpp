#include "FolderCompareScanner.h"
#include "FolderCompareModel.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
int fail(const QString &message) { QTextStream(stderr) << message << '\n'; return 1; }
FolderCompareState stateFor(const FolderCompareResult &result, const QString &path)
{
    for (const FolderCompareEntry &entry : result.entries) if (entry.relativePath == path) return entry.state;
    return FolderCompareState::InaccessibleLeft;
}
int rowFor(const FolderCompareModel &model, const QString &path)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), FolderCompareModel::RelativePathRole).toString() == path) return row;
    }
    return -1;
}
}

int main()
{
    QTemporaryDir temp;
    if (!temp.isValid()) return fail(QStringLiteral("temporary directory unavailable"));
    const QString left = temp.filePath(QStringLiteral("left"));
    const QString right = temp.filePath(QStringLiteral("right"));
    if (!writeFile(left + "/same.txt", "same") || !writeFile(right + "/same.txt", "same")
        || !writeFile(left + "/left.txt", "left") || !writeFile(right + "/right.txt", "right")
        || !writeFile(left + "/different.txt", "short") || !writeFile(right + "/different.txt", "longer")
        || !writeFile(left + "/nested/item.txt", "nested") || !writeFile(right + "/type", "file")) {
        return fail(QStringLiteral("could not create fixtures"));
    }
    QDir().mkpath(left + "/type");
#ifdef Q_OS_UNIX
    const auto makeLink = [](const QString &target, const QString &path) {
        const QByteArray encodedTarget = QFile::encodeName(target);
        const QByteArray encodedPath = QFile::encodeName(path);
        return ::symlink(encodedTarget.constData(), encodedPath.constData()) == 0;
    };
    if (!makeLink(QStringLiteral("same-target"), left + "/same-link")
        || !makeLink(QStringLiteral("same-target"), right + "/same-link")
        || !makeLink(QStringLiteral("left-target"), left + "/different-link")
        || !makeLink(QStringLiteral("right-target"), right + "/different-link")) {
        return fail(QStringLiteral("could not create symlink fixtures"));
    }
#endif

    FolderCompareResult flat = FolderCompareScanner::compare(left, right);
    const int expectedFlatEntries = 6
#ifdef Q_OS_UNIX
        + 2
#endif
        ;
    if (flat.entries.size() != expectedFlatEntries || stateFor(flat, "same.txt") != FolderCompareState::EqualMetadata
        || stateFor(flat, "left.txt") != FolderCompareState::LeftOnly
        || stateFor(flat, "right.txt") != FolderCompareState::RightOnly
        || stateFor(flat, "different.txt") != FolderCompareState::DifferentSize
        || stateFor(flat, "type") != FolderCompareState::TypeConflict
#ifdef Q_OS_UNIX
        || stateFor(flat, "same-link") != FolderCompareState::EqualMetadata
        || stateFor(flat, "different-link") != FolderCompareState::LinkConflict
#endif
        || stateFor(flat, "nested") != FolderCompareState::LeftOnly) {
        QStringList states;
        for (const FolderCompareEntry &entry : flat.entries) {
            states.append(entry.relativePath + QStringLiteral(":") + QString::number(static_cast<int>(entry.state)));
        }
        return fail(QStringLiteral("flat comparison states are incorrect: %1").arg(states.join(QStringLiteral(", "))));
    }
    FolderCompareOptions recursive;
    recursive.recursive = true;
    const FolderCompareResult deep = FolderCompareScanner::compare(left, right, recursive);
    if (stateFor(deep, "nested/item.txt") != FolderCompareState::LeftOnly) {
        return fail(QStringLiteral("recursive comparison missed nested file"));
    }
    if (!writeFile(left + "/content.txt", "aaaa") || !writeFile(right + "/content.txt", "bbbb")) return fail(QStringLiteral("could not create strict fixture"));
    QFile strictLeftSame(left + "/same.txt");
    QFile strictRightSame(right + "/same.txt");
    if (!strictLeftSame.open(QIODevice::ReadOnly) || !strictRightSame.open(QIODevice::ReadOnly)
        || !strictLeftSame.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime)
        || !strictRightSame.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-60), QFileDevice::FileModificationTime)) {
        return fail(QStringLiteral("could not set strict fixture modification times"));
    }
    FolderCompareOptions strict;
    strict.compareContents = true;
    const FolderCompareResult strictResult = FolderCompareScanner::compare(left, right, strict);
    if (stateFor(strictResult, "content.txt") != FolderCompareState::DifferentContent
        || stateFor(strictResult, "same.txt") != FolderCompareState::EqualContent) {
        return fail(QStringLiteral("strict comparison states are incorrect"));
    }
    FolderCompareModel strictPlanModel;
    strictPlanModel.setEntries(strictResult.entries);
    strictPlanModel.buildPlan(3);
    const int ambiguousContentRow = rowFor(strictPlanModel, QStringLiteral("content.txt"));
    if (ambiguousContentRow < 0
        || strictPlanModel.data(strictPlanModel.index(ambiguousContentRow), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::Unresolved)) {
        return fail(QStringLiteral("ambiguous strict-content difference was guessed"));
    }
    const QString leftTimestampPath = left + "/timestamp.txt";
    const QString rightTimestampPath = right + "/timestamp.txt";
    if (!writeFile(leftTimestampPath, "same") || !writeFile(rightTimestampPath, "same")) {
        return fail(QStringLiteral("could not create timestamp fixture"));
    }
    const QDateTime earlier = QDateTime::currentDateTimeUtc().addSecs(-60);
    const QDateTime later = QDateTime::currentDateTimeUtc();
    QFile leftTimestampFile(leftTimestampPath);
    QFile rightTimestampFile(rightTimestampPath);
    QFile leftDifferentFile(left + "/different.txt");
    QFile rightDifferentFile(right + "/different.txt");
    if (!leftTimestampFile.open(QIODevice::ReadOnly)
        || !rightTimestampFile.open(QIODevice::ReadOnly)
        || !leftDifferentFile.open(QIODevice::ReadOnly)
        || !rightDifferentFile.open(QIODevice::ReadOnly)
        || !leftTimestampFile.setFileTime(later, QFileDevice::FileModificationTime)
        || !rightTimestampFile.setFileTime(earlier, QFileDevice::FileModificationTime)
        || !leftDifferentFile.setFileTime(earlier, QFileDevice::FileModificationTime)
        || !rightDifferentFile.setFileTime(later, QFileDevice::FileModificationTime)) {
        return fail(QStringLiteral("could not set fixture modification times"));
    }
    const FolderCompareResult timestampResult = FolderCompareScanner::compare(left, right);
    if (stateFor(timestampResult, "timestamp.txt") != FolderCompareState::LeftNewer
        || stateFor(timestampResult, "different.txt") != FolderCompareState::RightNewer) {
        return fail(QStringLiteral("newer-side direction is reversed"));
    }
    FolderCompareModel planModel;
    planModel.setEntries(timestampResult.entries);
    planModel.buildPlan(3);
    const int leftOnlyRow = rowFor(planModel, QStringLiteral("left.txt"));
    const int rightOnlyRow = rowFor(planModel, QStringLiteral("right.txt"));
    const int newerRow = rowFor(planModel, QStringLiteral("timestamp.txt"));
    const int differentSizeRow = rowFor(planModel, QStringLiteral("different.txt"));
    const int conflictRow = rowFor(planModel, QStringLiteral("type"));
    if (leftOnlyRow < 0 || rightOnlyRow < 0 || newerRow < 0 || differentSizeRow < 0 || conflictRow < 0
        || planModel.data(planModel.index(leftOnlyRow), FolderCompareModel::PlannedActionRole).toInt() != static_cast<int>(FolderComparePlanAction::CopyLeftToRight)
        || planModel.data(planModel.index(rightOnlyRow), FolderCompareModel::PlannedActionRole).toInt() != static_cast<int>(FolderComparePlanAction::CopyRightToLeft)
        || planModel.data(planModel.index(newerRow), FolderCompareModel::PlannedActionRole).toInt() != static_cast<int>(FolderComparePlanAction::CopyLeftToRight)
        || planModel.data(planModel.index(differentSizeRow), FolderCompareModel::PlannedActionRole).toInt() != static_cast<int>(FolderComparePlanAction::CopyRightToLeft)
        || planModel.data(planModel.index(conflictRow), FolderCompareModel::PlannedActionRole).toInt() != static_cast<int>(FolderComparePlanAction::Unresolved)) {
        return fail(QStringLiteral("two-way plan mapping is incorrect"));
    }
    const int unresolvedBeforeSkip = planModel.unresolvedCount();
    planModel.setPlannedAction(conflictRow, static_cast<int>(FolderComparePlanAction::None));
    if (planModel.unresolvedCount() != unresolvedBeforeSkip - 1
        || planModel.data(planModel.index(rowFor(planModel, QStringLiteral("type"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::None)) {
        return fail(QStringLiteral("unresolved plan item could not be skipped"));
    }
    planModel.setPlannedAction(rowFor(planModel, QStringLiteral("type")), static_cast<int>(FolderComparePlanAction::CopyLeftToRight));
    if (planModel.data(planModel.index(rowFor(planModel, QStringLiteral("type"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::None)) {
        return fail(QStringLiteral("blocked plan item accepted an unsafe copy override"));
    }
    const QStringList planSources = planModel.plannedSources();
    const QStringList planDestinations = planModel.plannedDestinations(left, right);
    if (planSources.size() != planDestinations.size()
        || !planSources.contains(leftTimestampPath)
        || !planDestinations.contains(QDir(right).filePath(QStringLiteral("timestamp.txt")))) {
        return fail(QStringLiteral("exact plan destinations are incorrect"));
    }
    FolderCompareModel oneWayModel;
    oneWayModel.setEntries(timestampResult.entries);
    oneWayModel.buildPlan(1);
    if (oneWayModel.data(oneWayModel.index(rowFor(oneWayModel, QStringLiteral("timestamp.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::CopyRightToLeft)
        || oneWayModel.data(oneWayModel.index(rowFor(oneWayModel, QStringLiteral("left.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::None)) {
        return fail(QStringLiteral("update-left plan is incorrect"));
    }
    oneWayModel.buildPlan(2);
    if (oneWayModel.data(oneWayModel.index(rowFor(oneWayModel, QStringLiteral("different.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::CopyLeftToRight)
        || oneWayModel.data(oneWayModel.index(rowFor(oneWayModel, QStringLiteral("right.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::None)) {
        return fail(QStringLiteral("update-right plan is incorrect"));
    }
    FolderCompareModel missingModel;
    missingModel.setEntries(timestampResult.entries);
    missingModel.buildPlan(4);
    if (missingModel.plannedCount() != 1
        || !missingModel.plannedDestinationsWereAbsent()
        || missingModel.data(missingModel.index(rowFor(missingModel, QStringLiteral("right.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::CopyRightToLeft)
        || missingModel.data(missingModel.index(rowFor(missingModel, QStringLiteral("timestamp.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::None)
        || missingModel.unresolvedCount() != 0) {
        return fail(QStringLiteral("copy-missing-to-left plan is incorrect"));
    }
    missingModel.buildPlan(5);
    if (missingModel.plannedCount() != 2
        || !missingModel.plannedDestinationsWereAbsent()
        || missingModel.data(missingModel.index(rowFor(missingModel, QStringLiteral("left.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::CopyLeftToRight)
        || missingModel.data(missingModel.index(rowFor(missingModel, QStringLiteral("timestamp.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::None)
        || missingModel.unresolvedCount() != 0) {
        return fail(QStringLiteral("copy-missing-to-right plan is incorrect"));
    }
    FolderCompareModel recursivePlanModel;
    recursivePlanModel.setEntries(deep.entries);
    recursivePlanModel.buildPlan(5);
    const int nestedDirectoryRow = rowFor(recursivePlanModel, QStringLiteral("nested"));
    const int nestedFileRow = rowFor(recursivePlanModel, QStringLiteral("nested/item.txt"));
    if (nestedDirectoryRow < 0 || nestedFileRow < 0
        || recursivePlanModel.data(recursivePlanModel.index(nestedDirectoryRow), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::CopyLeftToRight)
        || recursivePlanModel.data(recursivePlanModel.index(nestedFileRow), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::None)) {
        return fail(QStringLiteral("directory plan did not suppress its descendants"));
    }
    recursivePlanModel.setPlannedAction(nestedDirectoryRow, static_cast<int>(FolderComparePlanAction::None));
    if (recursivePlanModel.data(recursivePlanModel.index(rowFor(recursivePlanModel, QStringLiteral("nested/item.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::CopyLeftToRight)) {
        return fail(QStringLiteral("directory override did not restore descendant actions"));
    }
    const int plannedBeforeFiltering = missingModel.plannedCount();
    missingModel.setFilterMode(4);
    const int expectedConflictCount = 1
#ifdef Q_OS_UNIX
        + 1
#endif
        ;
    if (missingModel.count() != expectedConflictCount || missingModel.plannedCount() != plannedBeforeFiltering) {
        return fail(QStringLiteral("conflict filtering changed the synchronization plan"));
    }
    missingModel.setFilterMode(0);
    missingModel.setSortMode(1);
    if (missingModel.count() != timestampResult.entries.size()
        || missingModel.plannedCount() != plannedBeforeFiltering) {
        return fail(QStringLiteral("sorting changed the synchronization plan"));
    }
    missingModel.markExecutionFailures({QDir(left).filePath(QStringLiteral("left.txt"))});
    const int failedRow = rowFor(missingModel, QStringLiteral("left.txt"));
    if (failedRow < 0
        || missingModel.data(missingModel.index(failedRow), FolderCompareModel::ExecutionErrorRole).toString() != QStringLiteral("Copy failed")) {
        return fail(QStringLiteral("failed synchronization item was not marked"));
    }
    leftTimestampFile.close();
    if (!writeFile(leftTimestampPath, "changed-size")) {
        return fail(QStringLiteral("could not mutate plan source"));
    }
    if (planModel.revalidatePlan(left, right) != 1
        || planModel.changedAfterCompareCount() != 1
        || planModel.data(planModel.index(rowFor(planModel, QStringLiteral("timestamp.txt"))), FolderCompareModel::PlannedActionRole).toInt()
            != static_cast<int>(FolderComparePlanAction::Unresolved)) {
        return fail(QStringLiteral("changed plan source was not invalidated"));
    }
    return 0;
}
