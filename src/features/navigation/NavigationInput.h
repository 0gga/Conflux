#pragma once
#include <QObject>
namespace studio {
class NavigationHistory;
class NavigationInput final : public QObject {
    Q_OBJECT
  public:
    explicit NavigationInput(NavigationHistory &history, QObject *parent = nullptr);
    void attach(QObject &window);

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    NavigationHistory &m_history;
};
} // namespace studio
