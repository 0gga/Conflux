#include "ApplicationController.h"
#include "Theme.h"
#include "miniz.h"
#include "services/PackContent.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)
using namespace studio;
class WorkspaceTests : public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    static void write(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    static void jar(const QString &path, const QByteArray &json) {
        mz_zip_archive zip{};
        QVERIFY(mz_zip_writer_init_file(&zip, path.toUtf8().constData(), 0));
        QVERIFY(mz_zip_writer_add_mem(&zip, "fabric.mod.json", json.constData(), size_t(json.size()),
                                      MZ_BEST_COMPRESSION));
        QVERIFY(mz_zip_writer_finalize_archive(&zip));
        QVERIFY(mz_zip_writer_end(&zip));
    }
  private slots:
    void forgeIdentityIsPreservedWithAuthorMetadata() {
        const auto path = storage.path() + "/forge-authors";
        QDir().mkpath(path + "/mods");
        const QByteArray metadata = "license=\"MIT\"\n[[dependencies.example]]\nmodId=\"dependency\"\n[[mods]"
                                    "]\nmodId=\"example\"\ndisplayName=\"Example "
                                    "Forge mod\"\nversion=\"1.0\"\nauthors=\"Author\"\n";
        mz_zip_archive zip{};
        QVERIFY(mz_zip_writer_init_file(&zip, (path + "/mods/example.jar").toUtf8().constData(), 0));
        QVERIFY(mz_zip_writer_add_mem(&zip, "META-INF/neoforge.mods.toml", metadata.constData(),
                                      size_t(metadata.size()), MZ_BEST_COMPRESSION));
        QVERIFY(mz_zip_writer_finalize_archive(&zip));
        QVERIFY(mz_zip_writer_end(&zip));
        const auto result = PackContent::scan({{"path", path}, {"loader", "neoforge"}});
        QCOMPARE(result.files.size(), 1);
        const auto mod = result.files.first().toMap();
        QCOMPARE(mod.value("modId").toString(), QString("example"));
        QCOMPARE(mod.value("author").toString(), QString("Author"));
        QVERIFY(mod.value("openSource").toBool());
        QVERIFY(result.coverage.startsWith("1 / 1"));
    }
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path() + "/settings");
    }
    void inspectRealArchivesAndPreserveFiles() {
        const auto path = storage.path() + "/content";
        QDir().mkpath(path + "/mods");
        const QByteArray fabric = R"({"id":"example","name":"Example mod","version":"1.2.3"})";
        jar(path + "/mods/one.jar", fabric);
        jar(path + "/mods/duplicate.jar", fabric);
        write(path + "/mods/unreadable.jar", "unrecognized archive");
        const auto duplicates = PackContent::scan({{"path", path}});
        bool found = false;
        for (const auto &value : duplicates.issues) {
            const auto entry = value.toMap();
            if (entry.value("category") == "duplicate") {
                QVERIFY(entry.value("message").toString().contains("mods/one.jar"));
                QVERIFY(entry.value("message").toString().contains("mods/duplicate.jar"));
                found = true;
            }
        }
        QVERIFY(found);

        write(path + "/config/example.json", "{\"unchanged\":true}");
        write(path + "/config/removedmod.toml", "unchanged=true");
        write(path + "/shaderpacks/shader.zip", "fixture");
        write(path + "/resourcepacks/resource.zip", "fixture");
        const auto result = PackContent::scan({{"path", path}, {"loader", "forge"}});
        QCOMPARE(result.files.size(), 7);
        QVERIFY(result.diagnostics.join(' ').contains("Multiple enabled files declare mod ID"));
        QVERIFY(result.diagnostics.join(' ').contains("declares fabric"));
        QVERIFY(result.diagnostics.join(' ').contains("ownership is uncertain"));
        QVERIFY(result.coverage.startsWith("2 / 3"));
        QFile config(path + "/config/removedmod.toml");
        QVERIFY(config.open(QIODevice::ReadOnly));
        QCOMPARE(config.readAll(), QByteArray("unchanged=true"));
        const auto fabricPack = PackContent::scan({{"path", path}, {"loader", "fabric"}});
        QVERIFY(!fabricPack.diagnostics.join(' ').contains("declares fabric"));
        const auto unknownPack = PackContent::scan({{"path", path}});
        QVERIFY(!unknownPack.diagnostics.join(' ').contains("declares fabric"));
    }
    void universalLoaderMetadataDoesNotProduceFalseMismatch() {
        const auto path = storage.path() + "/universal";
        QDir().mkpath(path + "/mods");
        mz_zip_archive zip{};
        QVERIFY(mz_zip_writer_init_file(&zip, (path + "/mods/universal.jar").toUtf8().constData(), 0));
        const QByteArray fabric = R"({"id":"universal","name":"Universal","version":"1"})";
        const QByteArray forge = R"([[mods]]
modId = "universal"
displayName = "Universal"
version = "1"
)";
        QVERIFY(mz_zip_writer_add_mem(&zip, "fabric.mod.json", fabric.constData(), fabric.size(), 0));
        QVERIFY(mz_zip_writer_add_mem(&zip, "META-INF/mods.toml", forge.constData(), forge.size(), 0));
        QVERIFY(mz_zip_writer_finalize_archive(&zip));
        QVERIFY(mz_zip_writer_end(&zip));
        for (const auto &loader : QStringList{"fabric", "forge", "neoforge"}) {
            const auto result = PackContent::scan({{"path", path}, {"loader", loader}});
            QVERIFY2(result.diagnostics.isEmpty(), qPrintable(result.diagnostics.join(' ')));
        }
    }
    void connectorMetadataDefersFabricCompatibilityToBridge() {
        const auto path = storage.path() + "/connector";
        QDir().mkpath(path + "/mods");
        jar(path + "/mods/fabric.jar", R"({"id":"fabric_example","name":"Example","version":"1"})");
        mz_zip_archive zip{};
        QVERIFY(mz_zip_writer_init_file(&zip, (path + "/mods/bridge.jar").toUtf8().constData(), 0));
        const QByteArray metadata =
            R"({"jars":[{"identifier":{"group":"org.sinytra","artifact":"connector-mod"}}]})";
        QVERIFY(mz_zip_writer_add_mem(&zip, "META-INF/jarjar/metadata.json", metadata.constData(),
                                      metadata.size(), 0));
        QVERIFY(mz_zip_writer_finalize_archive(&zip));
        QVERIFY(mz_zip_writer_end(&zip));
        const auto result = PackContent::scan({{"path", path}, {"loader", "neoforge"}});
        QVERIFY(result.diagnostics.join(' ').contains("bridge is present"));
        QVERIFY(!result.diagnostics.join(' ').contains("declares fabric"));
        for (const auto &row : result.files)
            if (row.toMap().value("fileName") == "fabric.jar")
                QCOMPARE(row.toMap().value("status").toString(), QString("Uses Connector"));
    }
    void embeddedIconsFollowIdentifiedConfigOwner() {
        const auto root = storage.path() + "/icons";
        QDir().mkpath(root + "/mods");
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        mz_zip_archive zip{};
        QVERIFY(mz_zip_writer_init_file(&zip, (root + "/mods/example.jar").toUtf8().constData(), 0));
        const QByteArray metadata =
            R"({"id":"example","name":"Example","version":"1","icon":"assets/example.png"})";
        QVERIFY(mz_zip_writer_add_mem(&zip, "fabric.mod.json", metadata.constData(), metadata.size(), 0));
        QVERIFY(mz_zip_writer_add_mem(&zip, "assets/example.png", png.constData(), png.size(), 0));
        QVERIFY(mz_zip_writer_finalize_archive(&zip));
        QVERIFY(mz_zip_writer_end(&zip));
        write(root + "/config/example-client.toml", "enabled=true\n");
        const auto result = PackContent::scan({{"path", root}, {"loader", "fabric"}});
        QVariantMap mod, config;
        for (const auto &item : result.files) {
            const auto row = item.toMap();
            if (row.value("category") == "mods")
                mod = row;
            else if (row.value("category") == "configs")
                config = row;
        }
        QVERIFY(mod.value("icon").toString().startsWith("data:image/png;base64,"));
        QCOMPARE(config.value("icon"), mod.value("icon"));
        QCOMPARE(config.value("relatedModId").toString(), QString("example"));
        QVERIFY(config.value("editable").toBool());
    }
    void missingManagedFilesAndDependencies() {
        const auto path = storage.path() + "/missing";
        QDir().mkpath(path);
        const QVariantMap dep{{"project_id", "dependency"}, {"dependency_type", "required"}};
        const QVariantMap mod{{"projectId", "parent"},
                              {"projectTitle", "Parent"},
                              {"installPath", "mods/absent.jar"},
                              {"dependencyRefs", QVariantList{dep}}};
        const auto result = PackContent::scan({{"path", path}, {"mods", QVariantList{mod}}});
        QVERIFY(result.diagnostics.join(' ').contains("file is missing"));
        QVERIFY(result.diagnostics.join(' ').contains("dependency dependency is missing"));
    }
    void hiddenPacksPersistAndBannerUsesAllSources() {
        const auto path = storage.path() + "/curseforge/Pack";
        write(path + "/minecraftinstance.json",
              R"({"name":"Launcher Pack","gameVersion":"1.21.1","baseModLoader":{"name":"fabric-0.16"}})");
        const auto settingsFile = storage.path() + "/controller.ini";
        QSettings settings(settingsFile, QSettings::IniFormat);
        settings.setValue("modrinthRoot", storage.path() + "/no-modrinth");
        settings.setValue("curseforgeRoot", storage.path() + "/curseforge");
        settings.sync();
        {
            ApplicationController app(nullptr, storage.path() + "/packs", settingsFile);
            app.refreshLibrary();
            QTRY_VERIFY(!app.libraryBusy());
            QCOMPARE(app.visiblePackCount(), 1);
            QCOMPARE(app.hiddenPackCount(), 0);
            QVERIFY(app.packs().isEmpty());
            QCOMPARE(app.libraryPacks().first().toMap().value("provider").toString(), QString("curseforge"));
            QQmlEngine engine;
            QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                                 "/pages/LibraryPage.qml"));
            QVERIFY2(component.isReady(), qPrintable(component.errorString()));
            QScopedPointer<QObject> page(
                component.createWithInitialProperties({{"app", QVariant::fromValue(&app)}}));
            QVERIFY2(page, qPrintable(component.errorString()));
            auto *item = qobject_cast<QQuickItem *>(page.data());
            QVERIFY(item);
            item->setWidth(1100);
            item->setHeight(720);
            auto *banner = page->findChild<QQuickItem *>("libraryOnboarding");
            QVERIFY(banner);
            QVERIFY(!banner->isVisible());
            app.setPackHidden(path, true);
            QCOMPARE(app.visiblePackCount(), 0);
            QCOMPARE(app.hiddenPackCount(), 1);
            QTRY_VERIFY(banner->isVisible());
            page->setProperty("hiddenTab", true);
            QCOMPARE(page->property("rows").value<QJSValue>().toVariant().toList().size(), 1);
            QSignalSpy openedState(&app, &ApplicationController::stateChanged);
            app.openLibraryPack(path);
            QVERIFY(openedState.count() > 0);
            QVERIFY(!app.activePack().value("managed").toBool());
            const auto before = app.activePack();
            app.saveChanges("blocked");
            app.removeMod("modrinth", "id");
            app.pinRelease(0);
            QCOMPARE(app.activePack(), before);
            QVERIFY(!QFileInfo::exists(path + "/.git"));
        }
        ApplicationController reopened(nullptr, storage.path() + "/packs", settingsFile);
        reopened.refreshLibrary();
        QTRY_VERIFY(!reopened.libraryBusy());
        QCOMPARE(reopened.hiddenPackCount(), 1);
        reopened.setPackHidden(path, false);
        QCOMPARE(reopened.visiblePackCount(), 1);
    }
    void themeValidationAndLastGoodState() {
        Theme theme(nullptr, storage.path() + "/theme");
        QVERIFY(!theme.colors().isEmpty());
        QCOMPARE(theme.colors().value("background").toString(), QString("#000000"));
        QCOMPARE(theme.file(), QString("Built-in OLED"));
        QFile input(QDir(theme.folder()).filePath("dark-grey.json"));
        QVERIFY(input.open(QIODevice::ReadOnly));
        auto doc = QJsonDocument::fromJson(input.readAll()).object();
        doc["name"] = "Test theme";
        auto surfaces = doc.value("surfaces").toObject();
        surfaces["input"] = "#555566";
        doc["surfaces"] = surfaces;
        const auto custom = QDir(theme.folder()).filePath("workspace-test.json");
        write(custom, QJsonDocument(doc).toJson());
        QVERIFY(theme.select("workspace-test.json"));
        QCOMPARE(theme.name(), QString("Test theme"));
        QCOMPARE(theme.colors().value("input").toString(), QString("#555566"));
        Theme reopened(nullptr, storage.path() + "/theme");
        QCOMPARE(reopened.name(), QString("Test theme"));
        const auto good = theme.colors();
        write(custom, "{invalid");
        QVERIFY(!theme.select("workspace-test.json"));
        QCOMPARE(theme.colors(), good);
        QVERIFY(!theme.error().isEmpty());
        QVERIFY(!theme.select("../outside.json"));
        QVERIFY(theme.select("Built-in dark grey"));
        QCOMPARE(theme.colors().value("background").toString(), QString("#17181b"));
        QFile::remove(custom);
    }
    void oledDefaultMigrationPreservesLaterChoicesAndCustomFiles() {
        QTemporaryDir legacy;
        QVERIFY(legacy.isValid());
        {
            QSettings settings(legacy.path() + "/appearance.ini", QSettings::IniFormat);
            settings.setValue("appearance/theme", "Built-in dark grey");
        }
        {
            Theme migrated(nullptr, legacy.path());
            QCOMPARE(migrated.file(), QString("Built-in OLED"));
            QVERIFY(migrated.select("Built-in dark grey"));
        }
        {
            Theme reopened(nullptr, legacy.path());
            QCOMPARE(reopened.file(), QString("Built-in dark grey"));
        }
        QTemporaryDir existingCustom;
        QVERIFY(existingCustom.isValid());
        {
            Theme seed(nullptr, existingCustom.path());
        }
        const auto customThemePath = existingCustom.path() + "/themes/dark-grey.json";
        QFile customThemeFile(customThemePath);
        QVERIFY(customThemeFile.open(QIODevice::ReadOnly));
        const auto customThemeBytes = customThemeFile.readAll();
        customThemeFile.close();
        {
            QSettings settings(existingCustom.path() + "/appearance.ini", QSettings::IniFormat);
            settings.remove("appearance/oledDefaultApplied");
            settings.setValue("appearance/theme", "dark-grey.json");
        }
        {
            Theme existing(nullptr, existingCustom.path());
            QCOMPARE(existing.file(), QString("dark-grey.json"));
            QCOMPARE(existing.colors().value("background").toString(), QString("#17181b"));
        }
        QVERIFY(customThemeFile.open(QIODevice::ReadOnly));
        QCOMPARE(customThemeFile.readAll(), customThemeBytes);
        QTemporaryDir customStorage;
        QVERIFY(customStorage.isValid());
        QDir().mkpath(customStorage.path() + "/themes");
        const auto customPath = customStorage.path() + "/themes/oled.json";
        const auto original = QByteArray("{\"user-edited\":true}");
        write(customPath, original);
        {
            QSettings settings(customStorage.path() + "/appearance.ini", QSettings::IniFormat);
            settings.setValue("appearance/theme", "oled.json");
        }
        Theme custom(nullptr, customStorage.path());
        QVERIFY(!custom.error().isEmpty());
        QFile originalFile(customPath);
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        QCOMPARE(originalFile.readAll(), original);
        QCOMPARE(custom.file(), QString("Built-in OLED"));
    }
};
QTEST_MAIN(WorkspaceTests)
#include "WorkspaceTests.moc"
