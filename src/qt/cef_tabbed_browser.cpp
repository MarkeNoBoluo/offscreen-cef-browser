#include "offscreen_cef/cef_tabbed_browser.h"

#include <QCloseEvent>
#include <QTabWidget>
#include <QVBoxLayout>

#include "offscreen_cef/cef_web_view.h"

namespace offscreen {

CefTabbedBrowser::CefTabbedBrowser(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  tabs_ = new QTabWidget(this);
  tabs_->setTabsClosable(true);
  tabs_->setMovable(true);
  layout->addWidget(tabs_);

  connect(tabs_, &QTabWidget::tabCloseRequested, this,
          &CefTabbedBrowser::CloseTab);
  connect(tabs_, &QTabWidget::currentChanged, this,
          &CefTabbedBrowser::UpdateCurrentState);
}

CefTabbedBrowser::~CefTabbedBrowser() = default;

CefWebView* CefTabbedBrowser::OpenTab(const QUrl& url, bool activate) {
  auto* view = new CefWebView(tabs_);
  const int index = tabs_->addTab(view, QStringLiteral("Loading..."));
  connect(view, &CefWebView::titleChanged, this, [this, view](const QString& title) {
    const int tab_index = tabs_->indexOf(view);
    if (tab_index >= 0) {
      tabs_->setTabText(tab_index, title.isEmpty() ? QStringLiteral("Loading...") : title);
    }
  });
  connect(view, &CefWebView::urlChanged, this, [this, view](const QUrl& changed_url) {
    if (tabs_->currentWidget() == view) {
      emit currentUrlChanged(changed_url);
    }
  });
  connect(view, &CefWebView::titleChanged, this, [this, view](const QString& title) {
    if (tabs_->currentWidget() == view) {
      emit currentTitleChanged(title);
    }
  });
  connect(view, &CefWebView::newWindowRequested, this,
          [this](const QUrl& popup_url) { OpenTab(popup_url, true); });
  connect(view, &CefWebView::browserClosed, this,
          [this, view]() { OnTabClosed(view); });
  connect(view, &CefWebView::loadingStateChanged, this,
          [this, view](bool isLoading, bool canGoBack, bool canGoForward) {
            if (tabs_->currentWidget() == view) {
              emit currentLoadingStateChanged(isLoading, canGoBack,
                                              canGoForward);
            }
          });
  connect(view, &CefWebView::loadError, this,
          [this, view](int errorCode, const QString& failedUrl,
                       const QString& errorText) {
            if (tabs_->currentWidget() == view) {
              emit currentLoadError(errorCode, failedUrl, errorText);
            }
          });
  connect(view, &CefWebView::downloadStateChanged, this,
          [this, view](int state, const QString& fileName,
                       const QString& fullPath) {
            if (tabs_->currentWidget() == view) {
              emit currentDownloadStateChanged(state, fileName, fullPath);
            }
          });
  connect(view, &CefWebView::contextMenuRequested, this,
          [this, view](const QPoint& globalPos) {
            if (tabs_->currentWidget() == view) {
              emit currentContextMenuRequested(globalPos);
            }
          });

  if (activate) {
    tabs_->setCurrentIndex(index);
  }
  view->LoadUrl(url);
  return view;
}

CefWebView* CefTabbedBrowser::CurrentView() const {
  return qobject_cast<CefWebView*>(tabs_->currentWidget());
}

int CefTabbedBrowser::tabCount() const {
  return tabs_->count();
}

void CefTabbedBrowser::CloseTab(int index) {
  auto* view = qobject_cast<CefWebView*>(tabs_->widget(index));
  if (view) {
    view->CloseBrowser();
  }
}

void CefTabbedBrowser::CloseAllTabs() {
  for (int index = 0; index < tabs_->count(); ++index) {
    CloseTab(index);
  }
  if (tabs_->count() == 0) {
    emit allTabsClosed();
  }
}

void CefTabbedBrowser::closeEvent(QCloseEvent* event) {
  if (tabs_->count() > 0) {
    close_requested_ = true;
    CloseAllTabs();
    event->ignore();
    return;
  }
  QWidget::closeEvent(event);
}

void CefTabbedBrowser::OnTabClosed(CefWebView* view) {
  const int index = tabs_->indexOf(view);
  if (index >= 0) {
    tabs_->removeTab(index);
  }
  view->deleteLater();
  if (tabs_->count() == 0) {
    emit allTabsClosed();
    if (close_requested_) {
      close_requested_ = false;
      close();
    }
  }
}

void CefTabbedBrowser::UpdateCurrentState(int index) {
  auto* view = qobject_cast<CefWebView*>(tabs_->widget(index));
  if (view) {
    emit currentUrlChanged(view->url());
    emit currentTitleChanged(view->title());
    emit currentLoadingStateChanged(view->isLoading(), view->canGoBack(),
                                    view->canGoForward());
  }
}

void CefTabbedBrowser::GoBack() {
  if (auto* view = CurrentView()) {
    view->GoBack();
  }
}

void CefTabbedBrowser::GoForward() {
  if (auto* view = CurrentView()) {
    view->GoForward();
  }
}

void CefTabbedBrowser::Reload() {
  if (auto* view = CurrentView()) {
    view->Reload();
  }
}

void CefTabbedBrowser::Stop() {
  if (auto* view = CurrentView()) {
    view->Stop();
  }
}

void CefTabbedBrowser::Copy() {
  if (auto* view = CurrentView()) {
    view->Copy();
  }
}

void CefTabbedBrowser::Cut() {
  if (auto* view = CurrentView()) {
    view->Cut();
  }
}

void CefTabbedBrowser::Paste() {
  if (auto* view = CurrentView()) {
    view->Paste();
  }
}

void CefTabbedBrowser::SelectAll() {
  if (auto* view = CurrentView()) {
    view->SelectAll();
  }
}

}  // namespace offscreen
