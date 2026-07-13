#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <atomic>

enum class FolderCompareState {
    EqualMetadata,
    EqualContent,
    LeftOnly,
    RightOnly,
    LeftNewer,
    RightNewer,
    DifferentSize,
    DifferentContent,
    TypeConflict,
    LinkConflict,
    InaccessibleLeft,
    InaccessibleRight,
    ChangedAfterCompare
};

enum class FolderComparePlanAction {
    None,
    CopyLeftToRight,
    CopyRightToLeft,
    Unresolved
};

struct FolderCompareOptions {
    bool recursive = false;
    bool includeHidden = false;
    bool compareTimestamps = true;
    bool compareContents = false;
    int timestampToleranceSeconds = 2;
};

struct FolderCompareEntry {
    QString relativePath;
    QString leftPath;
    QString rightPath;
    qint64 leftSize = -1;
    qint64 rightSize = -1;
    QDateTime leftModified;
    QDateTime rightModified;
    bool leftDirectory = false;
    bool rightDirectory = false;
    bool leftSymlink = false;
    bool rightSymlink = false;
    FolderCompareState state = FolderCompareState::EqualMetadata;
    FolderComparePlanAction plannedAction = FolderComparePlanAction::None;
    QString executionError;
};

struct FolderCompareResult {
    QList<FolderCompareEntry> entries;
    int inaccessibleLeft = 0;
    int inaccessibleRight = 0;
    QString error;
    bool cancelled = false;
};

class FolderCompareScanner final {
public:
    static FolderCompareResult compare(const QString &leftRoot,
                                       const QString &rightRoot,
                                       const FolderCompareOptions &options = {},
                                       const std::atomic_bool *cancelled = nullptr);
};
