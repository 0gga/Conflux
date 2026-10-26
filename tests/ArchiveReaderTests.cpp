#include "features/content/ArchiveReader.h"
#include "miniz.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
using namespace studio;
namespace {
bool archive(const QString &path, const QMap<QString, QByteArray> &entries, bool compress = true) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, QFile::encodeName(path).constData(), 0))
        return false;
    bool success = true;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        success &= mz_zip_writer_add_mem(&zip, it.key().toUtf8().constData(), it.value().constData(),
                                         size_t(it.value().size()), compress ? MZ_BEST_COMPRESSION : 0);
    success &= mz_zip_writer_finalize_archive(&zip);
    success &= mz_zip_writer_end(&zip);
    return success;
}
QByteArray png(const QSize &size) {
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    return image.save(&buffer, "PNG") ? bytes : QByteArray{};
}
} // namespace
class ArchiveReaderTests final : public QObject {
    Q_OBJECT
  private slots:
    void entryLimitsAreSignedAndInclusive_data() {
        QTest::addColumn<qint64>("limit");
        QTest::addColumn<bool>("emptyAllowed");
        QTest::addColumn<bool>("payloadAllowed");
        QTest::newRow("negative-one") << qint64(-1) << false << false;
        QTest::newRow("negative-many") << qint64(-4096) << false << false;
        QTest::newRow("minimum-signed") << qint64(std::numeric_limits<qsizetype>::min()) << false << false;
        QTest::newRow("zero") << qint64(0) << true << false;
        QTest::newRow("below-size") << qint64(2) << true << false;
        QTest::newRow("exact-size") << qint64(3) << true << true;
        QTest::newRow("above-size") << qint64(4) << true << true;
        QTest::newRow("maximum-signed") << qint64(std::numeric_limits<qsizetype>::max()) << true << true;
    }
    void entryLimitsAreSignedAndInclusive() {
        QFETCH(qint64, limit);
        QFETCH(bool, emptyAllowed);
        QFETCH(bool, payloadAllowed);
        QTemporaryDir temp;
        const auto path = temp.filePath("entries.zip");
        QVERIFY(archive(path, {{"empty", {}}, {"payload", "abc"}}));
        const auto result = ZipArchiveReader{}.readEntries(path, {"empty", "payload"}, qsizetype(limit));
        QCOMPARE(result.contains("empty"), emptyAllowed);
        QCOMPARE(result.contains("payload"), payloadAllowed);
        if (payloadAllowed)
            QCOMPARE(result.value("payload"), QByteArray("abc"));
    }
    void requestedEntriesRemainIndependentAndNeverExtractToDisk() {
        QTemporaryDir temp;
        const auto path = temp.filePath("entries.zip");
        const QMap<QString, QByteArray> entries{
            {"safe", "verified"}, {"nested/metadata", "nested"}, {"large", QByteArray(512 * 1024, 'x')}};
        QVERIFY(archive(path, entries));
        const auto result =
            ZipArchiveReader{}.readEntries(path,
                                           {"safe", "safe", "nested/metadata", "missing", "large",
                                            "../escape", "/absolute", "nested\\escape"},
                                           16);
        QCOMPARE(result, (QMap<QString, QByteArray>{{"safe", "verified"}, {"nested/metadata", "nested"}}));
        QCOMPARE(QDir(temp.path()).entryList(QDir::Files), QStringList{"entries.zip"});
        QVERIFY(QDir(temp.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(ZipArchiveReader{}.readEntries(path, {}, 16).isEmpty());
    }
    void missingAndMalformedArchivesAreQuiet_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("exists");
        QTest::newRow("missing") << QByteArray{} << false;
        QTest::newRow("empty") << QByteArray{} << true;
        QTest::newRow("text") << QByteArray("not an archive") << true;
        QTest::newRow("truncated-header") << QByteArray("PK\x03\x04") << true;
    }
    void missingAndMalformedArchivesAreQuiet() {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, exists);
        QTemporaryDir temp;
        const auto path = temp.filePath("invalid.zip");
        if (exists) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        }
        QVERIFY(ZipArchiveReader{}.readEntries(path, {"metadata"}, 1024).isEmpty());
    }
    void corruptEntryDoesNotHideAnotherVerifiedEntry() {
        QTemporaryDir temp;
        const auto path = temp.filePath("corrupt.zip");
        const QByteArray payload = "unique uncompressed metadata payload";
        QVERIFY(archive(path, {{"broken", payload}, {"valid", "good"}}, false));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        const auto bytes = file.readAll();
        const auto position = bytes.indexOf(payload);
        QVERIFY(position >= 0);
        QVERIFY(file.seek(position));
        QCOMPARE(file.write("!", 1), 1);
        file.close();
        QCOMPARE(ZipArchiveReader{}.readEntries(path, {"broken", "valid"}, 128),
                 (QMap<QString, QByteArray>{{"valid", "good"}}));
    }
    void unicodeArchivePathsUseTheNativeFileReader() {
        QTemporaryDir temp;
        const auto initial = temp.filePath("initial.zip");
        const auto path = temp.filePath(QString::fromUtf8("metadata-\xC3\xA6-\xE6\x97\xA5.jar"));
        QVERIFY(archive(initial, {{"metadata", "exact"}}));
        QVERIFY(QFile::rename(initial, path));
        QCOMPARE(ZipArchiveReader{}.readEntries(path, {"metadata"}, 5).value("metadata"),
                 QByteArray("exact"));
    }
    void rasterIconsAreBoundedPng_data() {
        QTest::addColumn<QSize>("sourceSize");
        QTest::addColumn<QSize>("resultSize");
        QTest::newRow("wide") << QSize(256, 128) << QSize(64, 32);
        QTest::newRow("tall") << QSize(128, 256) << QSize(32, 64);
        QTest::newRow("tiny") << QSize(1, 1) << QSize(64, 64);
        QTest::newRow("maximum-width") << QSize(4096, 64) << QSize(64, 1);
        QTest::newRow("maximum-height") << QSize(64, 4096) << QSize(1, 64);
        QTest::newRow("excessive-width") << QSize(4097, 64) << QSize{};
        QTest::newRow("excessive-height") << QSize(64, 4097) << QSize{};
    }
    void rasterIconsAreBoundedPng() {
        QFETCH(QSize, sourceSize);
        QFETCH(QSize, resultSize);
        const auto bytes = png(sourceSize);
        QVERIFY(!bytes.isEmpty());
        const auto icon = rasterIcon(bytes);
        if (resultSize.isEmpty()) {
            QVERIFY(icon.isEmpty());
            return;
        }
        const QString prefix = "data:image/png;base64,";
        QVERIFY(icon.startsWith(prefix));
        const auto image = QImage::fromData(QByteArray::fromBase64(icon.mid(prefix.size()).toLatin1()));
        QCOMPARE(image.size(), resultSize);
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::magenta));
    }
    void malformedOrVectorIconsUseTheQuietFallback() {
        for (const auto &bytes :
             {QByteArray{}, QByteArray("invalid"), png({64, 64}).left(32),
              QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'/>")})
            QVERIFY(rasterIcon(bytes).isEmpty());
    }
};
QTEST_GUILESS_MAIN(ArchiveReaderTests)
#include "ArchiveReaderTests.moc"
