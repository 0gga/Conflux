#pragma once
#include <QAbstractNativeEventFilter>
#include <QPointer>
#include <QWindow>
namespace studio {
// Native frame policy only; page navigation and close/unsaved guards stay in QML.
class WindowFrame final : public QAbstractNativeEventFilter {
  public:
    explicit WindowFrame(QWindow &window);
    ~WindowFrame() override;
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;

  private:
    QPointer<QWindow> m_window;
    WId m_nativeId = 0;
};
} // namespace studio
