#include "features/config/ConfigCodec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
using namespace studio;
class ConfigCodecTests final : public QObject {
    Q_OBJECT
  private slots:
    void jsonSupportedValuesRoundTripWithoutLosingType_data() {
        QTest::addColumn<QByteArray>("source");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("integer-min") << QByteArray("{\"a\":1}") << QVariant("-9223372036854775808")
                                     << QByteArray("{\"a\":-9223372036854775808}");
        QTest::newRow("integer-max") << QByteArray("{\"a\":1}") << QVariant("9223372036854775807")
                                     << QByteArray("{\"a\":9223372036854775807}");
        QTest::newRow("fraction") << QByteArray("{\"a\":1.25}") << QVariant("3.125")
                                  << QByteArray("{\"a\":3.125}");
        QTest::newRow("exponent") << QByteArray("{\"a\":1.25}") << QVariant("2.5e2")
                                  << QByteArray("{\"a\":250}");
        QTest::newRow("empty-list") << QByteArray("{\"a\":[\"old\"]}") << QVariant("[]")
                                    << QByteArray("{\"a\":[]}");
        QTest::newRow("longer-list") << QByteArray("{\"a\":[true]}") << QVariant("[false,true,false]")
                                     << QByteArray("{\"a\":[false,true,false]}");
        QTest::newRow("initially-empty") << QByteArray("{\"a\":[]}") << QVariant("[1,\"two\",true,null]")
                                         << QByteArray("{\"a\":[1,\"two\",true,null]}");
    }
    void jsonSupportedValuesRoundTripWithoutLosingType() {
        QFETCH(QByteArray, source);
        QFETCH(QVariant, value);
        QFETCH(QByteArray, expected);
        JsonConfigCodec codec;
        const auto result = codec.encode(source, {{"/a", value}});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(QJsonDocument::fromJson(result.bytes), QJsonDocument::fromJson(expected));
    }
    void rootArrayAddressesAndEditsEveryExactPosition() {
        JsonConfigCodec codec;
        const QByteArray source = R"([true,17,"original",{"a/b~c":false},["one","two"]])";
        const auto decoded = codec.decode(source);
        QVERIFY(decoded.error.isEmpty());
        QCOMPARE(decoded.fields.size(), 5);
        const QStringList pointers{"/0", "/1", "/2", "/3/a~1b~0c", "/4"};
        for (int i = 0; i < pointers.size(); ++i)
            QCOMPARE(decoded.fields[i].toMap().value("id").toString(), pointers[i]);
        const auto encoded = codec.encode(
            source,
            {{"/0", false}, {"/1", "23"}, {"/2", "changed"}, {"/3/a~1b~0c", true}, {"/4", "[\"three\"]"}});
        QVERIFY2(encoded.error.isEmpty(), qPrintable(encoded.error));
        const auto array = QJsonDocument::fromJson(encoded.bytes).array();
        QCOMPARE(array.size(), 5);
        QCOMPARE(array[0].toBool(), false);
        QCOMPARE(array[1].toInteger(), qint64(23));
        QCOMPARE(array[2].toString(), QString("changed"));
        QCOMPARE(array[3].toObject().value("a/b~c").toBool(), true);
        QCOMPARE(array[4].toArray(), QJsonArray{"three"});
    }
    void rootArrayChangesNeverAffectAdjacentValues_data() {
        QTest::addColumn<int>("position");
        for (int i = 0; i < 3; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rootArrayChangesNeverAffectAdjacentValues() {
        QFETCH(int, position);
        JsonConfigCodec codec;
        const auto encoded = codec.encode("[10,20,30]", {{"/" + QString::number(position), "99"}});
        QVERIFY2(encoded.error.isEmpty(), qPrintable(encoded.error));
        const auto array = QJsonDocument::fromJson(encoded.bytes).array();
        for (int i = 0; i < 3; ++i)
            QCOMPARE(array[i].toInteger(), qint64(i == position ? 99 : (i + 1) * 10));
    }
    void jsonInvalidTypedChangesAreRejected_data() {
        QTest::addColumn<QByteArray>("source");
        QTest::addColumn<QString>("pointer");
        QTest::addColumn<QVariant>("value");
        QTest::newRow("invalid-json") << QByteArray("{") << QString("/a") << QVariant(true);
        QTest::newRow("unknown") << QByteArray("{\"a\":true}") << QString("/missing") << QVariant(true);
        QTest::newRow("toggle-type") << QByteArray("{\"a\":true}") << QString("/a") << QVariant("false");
        QTest::newRow("integer-fraction") << QByteArray("{\"a\":2}") << QString("/a") << QVariant("2.5");
        QTest::newRow("integer-overflow")
            << QByteArray("{\"a\":2}") << QString("/a") << QVariant("9223372036854775808");
        QTest::newRow("non-finite") << QByteArray("{\"a\":2.5}") << QString("/a") << QVariant("nan");
        QTest::newRow("list-not-json") << QByteArray("{\"a\":[1]}") << QString("/a") << QVariant("[");
        QTest::newRow("list-not-array") << QByteArray("{\"a\":[1]}") << QString("/a") << QVariant("{}");
        QTest::newRow("list-object") << QByteArray("{\"a\":[1]}") << QString("/a") << QVariant("[{}]");
        QTest::newRow("nested-list") << QByteArray("{\"a\":[1]}") << QString("/a") << QVariant("[[1]]");
        QTest::newRow("list-type") << QByteArray("{\"a\":[\"one\"]}") << QString("/a") << QVariant("[1]");
        QTest::newRow("null") << QByteArray("{\"a\":null}") << QString("/a") << QVariant("changed");
    }
    void jsonInvalidTypedChangesAreRejected() {
        QFETCH(QByteArray, source);
        QFETCH(QString, pointer);
        QFETCH(QVariant, value);
        JsonConfigCodec codec;
        const auto result = codec.encode(source, {{pointer, value}});
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.bytes.isEmpty());
    }
    void jsonPreservesUnsupportedValuesAndLineEndings() {
        JsonConfigCodec codec;
        const QByteArray source =
            "{\r\n\"plain\":1,\"complex\":[{}],\"null\":null,\"large\":[9007199254740993]}\r\n";
        const auto decoded = codec.decode(source);
        QVERIFY(decoded.error.isEmpty());
        QCOMPARE(decoded.fields.size(), 1);
        QCOMPARE(decoded.unsupported, 3);
        const auto result = codec.encode(source, {{"/plain", "2"}});
        QVERIFY(result.error.isEmpty());
        QVERIFY(result.bytes.contains("\r\n"));
        auto expected = QJsonDocument::fromJson(source).object();
        expected["plain"] = 2;
        QCOMPARE(QJsonDocument::fromJson(result.bytes).object(), expected);
    }
    void jsonDecodeFieldLimitIsBoundedAndUnsupportedKeysStayUneditable() {
        JsonConfigCodec codec;
        QJsonObject object;
        for (int i = 0; i < 1005; ++i)
            object[QString("field%1").arg(i, 4, 10, QLatin1Char('0'))] = i;
        const auto source = QJsonDocument(object).toJson();
        const auto decoded = codec.decode(source);
        QCOMPARE(decoded.fields.size(), 1000);
        QCOMPARE(decoded.unsupported, 5);
        QVERIFY(!codec.encode(source, {{"/field1004", "3"}}).error.isEmpty());
        QVERIFY(codec.decode("[]").fields.isEmpty());
        QVERIFY(codec.decode("{}").fields.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ConfigCodecTests)
#include "ConfigCodecTests.moc"
