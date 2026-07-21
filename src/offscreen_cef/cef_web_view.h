#pragma once

#include <memory>

#include <QUrl>
#include <QWidget>

#include <windows.h>

namespace offscreen {

class BrowserImeHandler;
class BrowserService;
class BrowserWidget;

// A browser surface with no browser chrome. It is suitable for embedding in a
// layout, a stacked page, or a tab owned by the host application.
class CefWebView final : public QWidget {
  Q_OBJECT

 public:
  explicit CefWebView(QWidget* parent = nullptr);
  ~CefWebView() override;

  QUrl url() const;
  QString title() const;
  bool IsBrowserOpen() const;

 public slots:
  void LoadUrl(const QUrl& url);
  void Reload();
  void Stop();
  void CloseBrowser();

 signals:
  void urlChanged(const QUrl& url);
  void titleChanged(const QString& title);
  void loadFinished(bool ok);
  void loadFailed(const QString& error);
  void newWindowRequested(const QUrl& url);
  void browserClosed();

 protected:
  void showEvent(QShowEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  friend class CefRuntime;

  void StartBrowserIfReady();
  void OnBrowserClosed();
  bool HandleNativeImeMessage(MSG* message, long* result);

  std::unique_ptr<BrowserService> browser_service_;
  std::unique_ptr<BrowserImeHandler> ime_handler_;
  BrowserWidget* browser_widget_ = nullptr;
  QUrl pending_url_;
  bool browser_create_requested_ = false;
  bool browser_registered_ = false;
  bool close_requested_ = false;
  bool close_notified_ = false;
};

}  // namespace offscreen
