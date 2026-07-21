#pragma once

#include <QWidget>

class QTabWidget;
class QUrl;

namespace offscreen {

class CefWebView;

// A lightweight tab container. Hosts that provide their own toolbars can use
// this class while retaining complete control of navigation UI.
class CefTabbedBrowser final : public QWidget {
  Q_OBJECT

 public:
  explicit CefTabbedBrowser(QWidget* parent = nullptr);
  ~CefTabbedBrowser() override;

  CefWebView* OpenTab(const QUrl& url, bool activate = true);
  CefWebView* CurrentView() const;
  int tabCount() const;

 public slots:
  void CloseTab(int index);
  void CloseAllTabs();

 signals:
  void currentUrlChanged(const QUrl& url);
  void currentTitleChanged(const QString& title);
  void allTabsClosed();

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void OnTabClosed(CefWebView* view);
  void UpdateCurrentState(int index);

  QTabWidget* tabs_ = nullptr;
  bool close_requested_ = false;
};

}  // namespace offscreen
