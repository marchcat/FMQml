#include "BatchRenameEngine.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>

namespace {
bool expectName(const QList<BatchRenameEngine::RenamePreview> &preview, int index, const QString &expected)
{
    if (index < 0 || index >= preview.size()) {
        std::fprintf(stderr, "Missing preview row %d\n", index);
        return false;
    }
    if (preview.at(index).newName != expected) {
        std::fprintf(stderr,
                     "Unexpected name at %d expected %s got %s\n",
                     index,
                     qPrintable(expected),
                     qPrintable(preview.at(index).newName));
        return false;
    }
    if (preview.at(index).hasConflict) {
        std::fprintf(stderr, "Unexpected conflict at %d: %s\n", index, qPrintable(preview.at(index).error));
        return false;
    }
    return true;
}

QVariantMap rule(QVariantMap values)
{
    return values;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    BatchRenameEngine engine;

    const auto literalPreview = engine.generatePreview(
        {QStringLiteral("/tmp/Holiday Photo.jpeg")},
        {rule({{QStringLiteral("type"), QStringLiteral("replace")},
               {QStringLiteral("search"), QStringLiteral("holiday")},
               {QStringLiteral("replace"), QStringLiteral("Summer")},
               {QStringLiteral("caseSensitive"), false},
               {QStringLiteral("regex"), false}})});
    if (!expectName(literalPreview, 0, QStringLiteral("Summer Photo.jpeg"))) return 1;

    const auto formatPreview = engine.generatePreview(
        {QStringLiteral("/tmp/report.tar.gz")},
        {rule({{QStringLiteral("type"), QStringLiteral("format")},
               {QStringLiteral("prefix"), QStringLiteral("final-")},
               {QStringLiteral("suffix"), QStringLiteral("-signed")}})});
    if (!expectName(formatPreview, 0, QStringLiteral("final-report.tar-signed.gz"))) return 1;

    const auto prefixNumberPreview = engine.generatePreview(
        {QStringLiteral("/tmp/photo.png")},
        {rule({{QStringLiteral("type"), QStringLiteral("numbering")},
               {QStringLiteral("start"), 7},
               {QStringLiteral("padding"), 3},
               {QStringLiteral("position"), QStringLiteral("prefix")}})});
    if (!expectName(prefixNumberPreview, 0, QStringLiteral("007photo.png"))) return 1;

    const auto templatePreview = engine.generatePreview(
        {QStringLiteral("/tmp/old-a.txt"), QStringLiteral("/tmp/old-b.txt")},
        {rule({{QStringLiteral("type"), QStringLiteral("template")},
               {QStringLiteral("text"), QStringLiteral("Document-")},
               {QStringLiteral("start"), 4},
               {QStringLiteral("padding"), 2}})});
    if (!expectName(templatePreview, 0, QStringLiteral("Document-04.txt"))
        || !expectName(templatePreview, 1, QStringLiteral("Document-05.txt"))) return 1;

    const auto unchangedPreview = engine.generatePreview({QStringLiteral("/tmp/same.txt")}, {});
    if (!expectName(unchangedPreview, 0, QStringLiteral("same.txt"))) return 1;

    QVariantList stackedRules;
    stackedRules << rule({
        {QStringLiteral("type"), QStringLiteral("replace")},
        {QStringLiteral("search"), QStringLiteral("^IMG_(\\d+)$")},
        {QStringLiteral("replace"), QStringLiteral("photo_$1")},
        {QStringLiteral("caseSensitive"), false},
        {QStringLiteral("regex"), true},
    });
    stackedRules << rule({
        {QStringLiteral("type"), QStringLiteral("transform")},
        {QStringLiteral("mode"), QStringLiteral("uppercase")},
    });
    stackedRules << rule({
        {QStringLiteral("type"), QStringLiteral("numbering")},
        {QStringLiteral("start"), 1},
        {QStringLiteral("padding"), 2},
        {QStringLiteral("position"), QStringLiteral("suffix")},
    });

    const auto stackedPreview = engine.generatePreview({
        QStringLiteral("/tmp/IMG_1001.jpg"),
        QStringLiteral("/tmp/IMG_1002.jpg"),
    }, stackedRules);
    if (!expectName(stackedPreview, 0, QStringLiteral("PHOTO_100101.jpg"))
        || !expectName(stackedPreview, 1, QStringLiteral("PHOTO_100202.jpg"))) {
        return 1;
    }

    QVariantList cleanupRules;
    cleanupRules << rule({
        {QStringLiteral("type"), QStringLiteral("transform")},
        {QStringLiteral("mode"), QStringLiteral("spaces-dash")},
    });
    cleanupRules << rule({
        {QStringLiteral("type"), QStringLiteral("transform")},
        {QStringLiteral("mode"), QStringLiteral("lowercase")},
    });
    const auto cleanupPreview = engine.generatePreview({QStringLiteral("/tmp/My  File Name.txt")}, cleanupRules);
    if (!expectName(cleanupPreview, 0, QStringLiteral("my-file-name.txt"))) {
        return 1;
    }

    QVariantList badRegexRules;
    badRegexRules << rule({
        {QStringLiteral("type"), QStringLiteral("replace")},
        {QStringLiteral("search"), QStringLiteral("(")},
        {QStringLiteral("replace"), QString()},
        {QStringLiteral("regex"), true},
    });
    const auto badRegexPreview = engine.generatePreview({QStringLiteral("/tmp/file.txt")}, badRegexRules);
    if (badRegexPreview.isEmpty() || !badRegexPreview.at(0).hasConflict
        || !badRegexPreview.at(0).error.startsWith(QStringLiteral("Invalid regular expression:"))) {
        std::fprintf(stderr, "Invalid regex was not reported\n");
        return 1;
    }

    const auto duplicatePreview = engine.generatePreview(
        {QStringLiteral("/tmp/A.txt"), QStringLiteral("/tmp/B.txt")},
        {rule({{QStringLiteral("type"), QStringLiteral("replace")},
               {QStringLiteral("search"), QStringLiteral("A|B")},
               {QStringLiteral("replace"), QStringLiteral("same")},
               {QStringLiteral("caseSensitive"), true},
               {QStringLiteral("regex"), true}})});
    if (duplicatePreview.size() != 2 || !duplicatePreview.at(1).hasConflict
        || duplicatePreview.at(1).error != QStringLiteral("Duplicate name in batch")) {
        std::fprintf(stderr, "Duplicate destination was not reported\n");
        return 1;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) return 1;
    const QString sourcePath = tempDir.filePath(QStringLiteral("source.txt"));
    const QString occupiedPath = tempDir.filePath(QStringLiteral("occupied.txt"));
    QFile source(sourcePath);
    QFile occupied(occupiedPath);
    if (!source.open(QIODevice::WriteOnly) || !occupied.open(QIODevice::WriteOnly)) return 1;
    source.close();
    occupied.close();
    const auto filesystemConflict = engine.generatePreview(
        {sourcePath},
        {rule({{QStringLiteral("type"), QStringLiteral("replace")},
               {QStringLiteral("search"), QStringLiteral("source")},
               {QStringLiteral("replace"), QStringLiteral("occupied")}})});
    if (filesystemConflict.size() != 1 || !filesystemConflict.first().hasConflict
        || filesystemConflict.first().error != QStringLiteral("File already exists")) {
        std::fprintf(stderr, "Existing filesystem destination was not reported\n");
        return 1;
    }

    return 0;
}
