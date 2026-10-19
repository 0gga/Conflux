#pragma once
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>
class QGuiApplication;
class QQuickWindow;
namespace studio {
class ApplicationController;
class DesktopSession;
// Runtime verification is isolated from normal startup and feature policies.
class DesktopSmokeTest final : public QObject {
  public:
    explicit DesktopSmokeTest(const QStringList &arguments, QObject *parent = nullptr);
    bool enabled() const {
        return !m_output.isEmpty();
    }
    QString storagePath() const {
        return m_storage ? m_storage->path() : QString{};
    }
    void start(QGuiApplication &, QQuickWindow *, ApplicationController &, DesktopSession &);

  private:
    QString m_output;
    std::unique_ptr<QTemporaryDir> m_storage;
};
} // namespace studio
