#include "FileTypeIconResolver.h"

#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QStringList>

namespace {
struct FileIconRule {
    QString iconName;
    QStringList extensions;
};

QString fileTypeIconPath(const QString &name)
{
    return QStringLiteral("qrc:/qt/qml/FM/qml/assets/filetypes/%1.svg").arg(name);
}

const QList<FileIconRule> &nativeIconOverrideRules()
{
    static const QList<FileIconRule> rules = {
        {QStringLiteral("archive"), {QStringLiteral("apk")}},
        {QStringLiteral("fb2"), {QStringLiteral("fb2"), QStringLiteral("fb2.zip")}},
    };
    return rules;
}

QString normalizedExtension(QString extension)
{
    extension = extension.trimmed().toLower();
    while (extension.startsWith(QLatin1Char('.'))) {
        extension.remove(0, 1);
    }
    return extension;
}

bool fileNameMatchesExtension(const QString &fileName, const QString &extension)
{
    const QString ext = normalizedExtension(extension);
    return !ext.isEmpty() && fileName.toLower().endsWith(QLatin1Char('.') + ext);
}

QString matchingRuleIconForExtension(const QString &extension, const QList<FileIconRule> &rules)
{
    const QString ext = normalizedExtension(extension);
    if (ext.isEmpty()) {
        return {};
    }

    for (const FileIconRule &rule : rules) {
        for (const QString &ruleExtension : rule.extensions) {
            if (ext == normalizedExtension(ruleExtension)) {
                return rule.iconName;
            }
        }
    }
    return {};
}

QString matchingRuleIconForFileName(const QString &fileName, const QList<FileIconRule> &rules)
{
    if (fileName.isEmpty()) {
        return {};
    }

    for (const FileIconRule &rule : rules) {
        for (const QString &extension : rule.extensions) {
            if (fileNameMatchesExtension(fileName, extension)) {
                return rule.iconName;
            }
        }
    }
    return {};
}

bool hasSuffix(const QString &suffix, const QSet<QString> &suffixes)
{
    return suffixes.contains(suffix.toLower());
}

QString fileNameFromPathHint(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int archiveSeparator = path.lastIndexOf(QStringLiteral("|/"));
    if (archiveSeparator >= 0) {
        path = path.mid(archiveSeparator + 2);
    }

    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : QFileInfo(path).fileName();
}
}

FileTypeIconResolver::FileTypeIconResolver(QObject *parent)
    : QObject(parent)
{
}

QString FileTypeIconResolver::iconForSuffix(const QString &suffix, bool isDirectory) const
{
    if (isDirectory) {
        return fileTypeIconPath(QStringLiteral("folder"));
    }

    const QString s = suffix.toLower();
    const QString explicitIcon = matchingRuleIconForExtension(s, nativeIconOverrideRules());
    if (!explicitIcon.isEmpty()) {
        return fileTypeIconPath(explicitIcon);
    }

    static const QSet<QString> imageSuffixes = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("gif"),
        QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("ico"), QStringLiteral("svg"),
        QStringLiteral("svgz"), QStringLiteral("avif"), QStringLiteral("heic"), QStringLiteral("heif"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("raw"), QStringLiteral("cr2"),
        QStringLiteral("nef"), QStringLiteral("dng")
    };
    static const QSet<QString> audioSuffixes = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("m4a"), QStringLiteral("m4b"), QStringLiteral("wav"), QStringLiteral("wma"),
        QStringLiteral("aac"), QStringLiteral("opus"), QStringLiteral("aiff"), QStringLiteral("aif"),
        QStringLiteral("mid"), QStringLiteral("midi")
    };
    static const QSet<QString> videoSuffixes = {
        QStringLiteral("mp4"), QStringLiteral("avi"), QStringLiteral("mkv"), QStringLiteral("mov"),
        QStringLiteral("wmv"), QStringLiteral("webm"), QStringLiteral("flv"), QStringLiteral("m4v"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("3gp"), QStringLiteral("ts")
    };
    static const QSet<QString> archiveSuffixes = {
        QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"), QStringLiteral("tar"),
        QStringLiteral("gz"), QStringLiteral("tgz"), QStringLiteral("bz2"), QStringLiteral("xz"),
        QStringLiteral("cab"), QStringLiteral("iso"), QStringLiteral("img"), QStringLiteral("vhd"),
        QStringLiteral("vhdx"), QStringLiteral("wim")
    };
    static const QSet<QString> spreadsheetSuffixes = {
        QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("xlsm"), QStringLiteral("csv"),
        QStringLiteral("ods"), QStringLiteral("tsv")
    };
    static const QSet<QString> presentationSuffixes = {
        QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("pps"), QStringLiteral("ppsx"),
        QStringLiteral("odp")
    };
    static const QSet<QString> codeSuffixes = {
        QStringLiteral("js"), QStringLiteral("mjs"), QStringLiteral("cjs"), QStringLiteral("ts"),
        QStringLiteral("tsx"), QStringLiteral("jsx"), QStringLiteral("html"), QStringLiteral("htm"),
        QStringLiteral("css"), QStringLiteral("scss"), QStringLiteral("sass"), QStringLiteral("less"),
        QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("toml"), QStringLiteral("ini"), QStringLiteral("py"), QStringLiteral("cpp"),
        QStringLiteral("cxx"), QStringLiteral("cc"), QStringLiteral("c"), QStringLiteral("h"),
        QStringLiteral("hpp"), QStringLiteral("cs"), QStringLiteral("java"), QStringLiteral("go"),
        QStringLiteral("rs"), QStringLiteral("php"), QStringLiteral("rb"), QStringLiteral("sh"),
        QStringLiteral("sql")
    };
    static const QSet<QString> fontSuffixes = {
        QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("woff"), QStringLiteral("woff2"),
        QStringLiteral("fon")
    };
    static const QSet<QString> executableSuffixes = {
        QStringLiteral("exe"), QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("ps1"),
        QStringLiteral("com"), QStringLiteral("msi"), QStringLiteral("dll"), QStringLiteral("sys"),
        QStringLiteral("appx"), QStringLiteral("msix"), QStringLiteral("lnk")
    };

    if (hasSuffix(s, imageSuffixes)) return fileTypeIconPath(QStringLiteral("image"));
    if (hasSuffix(s, audioSuffixes)) return fileTypeIconPath(QStringLiteral("music"));
    if (hasSuffix(s, videoSuffixes)) return fileTypeIconPath(QStringLiteral("video"));
    if (hasSuffix(s, archiveSuffixes)) return fileTypeIconPath(QStringLiteral("archive"));
    if (s == QStringLiteral("pdf")) return fileTypeIconPath(QStringLiteral("pdf"));
    if (hasSuffix(s, spreadsheetSuffixes)) return fileTypeIconPath(QStringLiteral("spreadsheet"));
    if (hasSuffix(s, presentationSuffixes)) return fileTypeIconPath(QStringLiteral("presentation"));
    if (hasSuffix(s, codeSuffixes)) return fileTypeIconPath(QStringLiteral("code"));
    if (hasSuffix(s, fontSuffixes)) return fileTypeIconPath(QStringLiteral("font"));
    if (hasSuffix(s, executableSuffixes)) return fileTypeIconPath(QStringLiteral("executable"));
    return fileTypeIconPath(QStringLiteral("document"));
}

QString FileTypeIconResolver::iconForPath(const QString &path) const
{
    const QFileInfo info(path);
    const QString explicitIcon = info.isDir()
        ? QString{}
        : matchingRuleIconForFileName(info.fileName(), nativeIconOverrideRules());
    if (!explicitIcon.isEmpty()) {
        return fileTypeIconPath(explicitIcon);
    }
    return iconForSuffix(info.suffix(), info.isDir());
}

QString FileTypeIconResolver::iconForPathHint(const QString &path, bool isDirectory) const
{
    const QString fileName = fileNameFromPathHint(path);
    const QString explicitIcon = isDirectory
        ? QString{}
        : matchingRuleIconForFileName(fileName, nativeIconOverrideRules());
    if (!explicitIcon.isEmpty()) {
        return fileTypeIconPath(explicitIcon);
    }
    return iconForSuffix(QFileInfo(fileName).suffix(), isDirectory);
}

QString FileTypeIconResolver::nativeIconOverrideForPathHint(const QString &path, bool isDirectory) const
{
    if (isDirectory) {
        return {};
    }

    const QString iconName = matchingRuleIconForFileName(fileNameFromPathHint(path), nativeIconOverrideRules());
    return iconName.isEmpty() ? QString{} : fileTypeIconPath(iconName);
}
