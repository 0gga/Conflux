#pragma once
#include "domain/CatalogueSearch.h"
#include "features/content/ContentListModel.h"
#include <QObject>
#include <memory>
namespace studio {
class ShortcutPreferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap bindings READ bindings NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(bool capturing READ capturing NOTIFY captureChanged)
    Q_PROPERTY(QString recordingTitle READ recordingTitle NOTIFY captureChanged)
    Q_PROPERTY(QString candidate READ candidate NOTIFY captureChanged)
    Q_PROPERTY(QString error READ error NOTIFY captureChanged)
  public:
    explicit ShortcutPreferences(std::shared_ptr<IBrowseSettingsStore>, QObject *parent = nullptr);
    QVariantMap bindings() const {
        return m_bindings;
    }
    QVariantList rows() const;
    QAbstractItemModel *model() {
        return &m_model;
    }
    bool capturing() const {
        return !m_recording.isEmpty();
    }
    QString recordingTitle() const;
    QString candidate() const;
    QString error() const {
        return m_error;
    }
    Q_INVOKABLE bool setBinding(const QString &action, const QString &sequence);
    Q_INVOKABLE bool reset(const QString &action);
    Q_INVOKABLE bool resetAll();
    Q_INVOKABLE void beginCapture(const QString &action);
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void capturePress(int key, int modifiers);
    Q_INVOKABLE bool captureRelease(int key);
    Q_INVOKABLE bool clearCaptured();
  signals:
    void changed();
    void captureChanged();

  private:
    std::shared_ptr<IBrowseSettingsStore> m_store;
    ContentListModel m_model;
    QVariantMap m_bindings, m_overrides;
    QString m_recording, m_candidate, m_error;
    int m_candidateKey = 0;
    quint64 m_captureGeneration = 0;
    static QVariantList definitions();
    static QVariantMap defaults();
    static QString validate(const QString &, QString *canonical);
    bool persist(const QVariantMap &, const QVariantMap &);
};
} // namespace studio
