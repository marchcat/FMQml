#pragma once

#include <QVariantList>
#include <QString>

struct FileAttributesInfo {
    bool hidden = false;
    bool readOnly = false;
    bool system = false;
    bool archive = false;
};

struct FileAccessInfo {
    enum class State {
        Unknown,
        Denied,
        Allowed
    };

    bool canRead = false;
    bool canModify = false;
    bool canDelete = false;
    bool canExecute = false;
    bool canBrowse = false;
    bool canCreateChildren = false;
    bool canTraverse = false;
    bool canChangeAttributes = false;
    bool exact = false;
    State readState = State::Unknown;
    State modifyState = State::Unknown;
    State deleteState = State::Unknown;
    State executeState = State::Unknown;
    State browseState = State::Unknown;
    State createChildrenState = State::Unknown;
    State traverseState = State::Unknown;
    State changeAttributesState = State::Unknown;
};

struct FileCapabilityInfo {
    QString path;
    bool exists = false;
    bool isDirectory = false;
    bool isArchiveLike = false;
    FileAttributesInfo attributes;
    FileAccessInfo access;
    QString accessSummary;
    QString attributesSummary;
};

class FileAccessResolver
{
public:
    static FileCapabilityInfo resolve(const QString &path);
    static QVariantList accessProperties(const FileCapabilityInfo &info);
    static QVariantList attributeProperties(const FileCapabilityInfo &info);
    static bool setHidden(const QString &path, bool enabled, QString *error = nullptr);
    static bool setReadOnly(const QString &path, bool enabled, QString *error = nullptr);
    static void invalidate(const QString &path);
    static void invalidateAll();
};
