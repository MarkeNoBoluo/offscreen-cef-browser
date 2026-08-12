#include "qt/browser_widget.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <sstream>
#include <utility>

#include "app/diagnostic_log.h"
#include "browser/browser_frame.h"
#include "browser/gpu_copy_policy.h"
#include "browser/gpu_frame_bridge.h"
#include "browser/browser_ime_core.h"
#include "browser/browser_ime_handler.h"
#include "browser/browser_input_mapping.h"
#include "browser/browser_service.h"
#include "browser/osr_render_log.h"
#include "browser/render_stats_log.h"
#include "qt/browser_gl_renderer.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <QWheelEvent>

namespace offscreen {

namespace {

// cef_cursor_type_t values (from CEF 96 cef_types.h)
constexpr int kCursorPointer = 0;            // CT_POINTER
constexpr int kCursorCross = 1;              // CT_CROSS
constexpr int kCursorHand = 2;               // CT_HAND
constexpr int kCursorIBeam = 3;              // CT_IBEAM
constexpr int kCursorWait = 4;               // CT_WAIT
constexpr int kCursorHelp = 5;               // CT_HELP
constexpr int kCursorEastResize = 6;         // CT_EASTRESIZE
constexpr int kCursorNorthResize = 7;        // CT_NORTHRESIZE
constexpr int kCursorNorthEastResize = 8;    // CT_NORTHEASTRESIZE
constexpr int kCursorNorthWestResize = 9;    // CT_NORTHWESTRESIZE
constexpr int kCursorSouthResize = 10;       // CT_SOUTHRESIZE
constexpr int kCursorSouthEastResize = 11;   // CT_SOUTHEASTRESIZE
constexpr int kCursorSouthWestResize = 12;   // CT_SOUTHWESTRESIZE
constexpr int kCursorWestResize = 13;        // CT_WESTRESIZE
constexpr int kCursorNorthSouthResize = 14;  // CT_NORTHSOUTHRESIZE
constexpr int kCursorEastWestResize = 15;    // CT_EASTWESTRESIZE
constexpr int kCursorMove = 29;              // CT_MOVE
constexpr int kCursorVerticalText = 30;      // CT_VERTICALTEXT
constexpr int kCursorCell = 31;              // CT_CELL
constexpr int kCursorContextMenu = 32;       // CT_CONTEXTMENU
constexpr int kCursorAlias = 33;             // CT_ALIAS
constexpr int kCursorProgress = 34;          // CT_PROGRESS
constexpr int kCursorNoDrop = 35;            // CT_NODROP
constexpr int kCursorCopy = 36;              // CT_COPY
constexpr int kCursorNone = 37;              // CT_NONE
constexpr int kCursorNotAllowed = 38;        // CT_NOTALLOWED
constexpr int kCursorZoomIn = 39;            // CT_ZOOMIN
constexpr int kCursorZoomOut = 40;           // CT_ZOOMOUT
constexpr int kCursorGrab = 41;              // CT_GRAB
constexpr int kCursorGrabbing = 42;          // CT_GRABBING

CefRenderHandler::DragOperation PreferredDragOperation(
    CefRenderHandler::DragOperationsMask allowed_ops) {
  if (allowed_ops & DRAG_OPERATION_COPY) return DRAG_OPERATION_COPY;
  if (allowed_ops & DRAG_OPERATION_MOVE) return DRAG_OPERATION_MOVE;
  if (allowed_ops & DRAG_OPERATION_LINK) return DRAG_OPERATION_LINK;
  if (allowed_ops & DRAG_OPERATION_GENERIC) return DRAG_OPERATION_GENERIC;
  if (allowed_ops & DRAG_OPERATION_PRIVATE) return DRAG_OPERATION_PRIVATE;
  return DRAG_OPERATION_NONE;
}

}  // namespace

// --- IME composition message handler ---

void BrowserWidget::HandleImeCompositionMessage(WPARAM wParam,
                                                 LPARAM lParam) {
  DiagnosticLog("BrowserWidget::HandleImeCompositionMessage wParam=" +
                HexValue(static_cast<uintptr_t>(wParam)) + " lParam=" +
                HexValue(static_cast<uintptr_t>(lParam)) +
                " has_ime_handler=" +
                (ime_handler_ ? std::string("true") : std::string("false")) +
                " has_browser_service=" +
                (browser_service_ ? std::string("true")
                                  : std::string("false")));
  if (!ime_handler_ || !browser_service_) return;

  std::wstring commit_text;
  std::wstring composition_text;
  std::vector<CefCompositionUnderline> underlines;
  CefRange selection_range(0, 0);

  const bool handled = ime_handler_->HandleImeComposition(
      lParam, commit_text, composition_text, underlines, selection_range);

  DiagnosticLog("BrowserWidget::HandleImeCompositionMessage handled=" +
                std::string(handled ? "true" : "false") +
                " commit_len=" + std::to_string(commit_text.size()) +
                " comp_len=" + std::to_string(composition_text.size()) +
                " underlines=" + std::to_string(underlines.size()) +
                " selection=" + std::to_string(selection_range.from) + "-" +
                std::to_string(selection_range.to));

  if (!handled) return;

  const CefRange invalid_range(UINT32_MAX, UINT32_MAX);

  // Result string (committed text): commit first
  if (!commit_text.empty()) {
    browser_service_->ImeCommitText(commit_text, invalid_range, 0);
    if (composition_text.empty()) {
      ime_handler_->ResetComposition();
    }
  }

  // Composition string (in-progress text): update after result
  if (!composition_text.empty()) {
    browser_service_->ImeSetComposition(composition_text, underlines,
                                        invalid_range, selection_range);
    return;
  }

  // Both texts empty: if composition flag was set, it's just starting -
  // don't cancel. Otherwise it's a genuine cancellation.
  if (commit_text.empty()) {
    if (ImeHasCompositionString(lParam)) {
      // Composition is active but text is empty, initial state, wait
      return;
    }
    is_composing_ = false;
    browser_service_->ImeCancelComposition();
    ime_handler_->CancelComposition();
  }
}

BrowserWidget::BrowserWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      gl_renderer_(std::make_unique<BrowserGlRenderer>()) {
  setAttribute(Qt::WA_NativeWindow, true);
  setAttribute(Qt::WA_DontCreateNativeAncestors, false);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  setAcceptDrops(true);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAutoFillBackground(false);
  stats_timer_ = new QTimer(this);
  stats_timer_->setInterval(1000);
  connect(stats_timer_, &QTimer::timeout, this, &BrowserWidget::EmitRenderStats);
  stats_timer_->start();
  DiagnosticLog("BrowserWidget constructed");
}

BrowserWidget::~BrowserWidget() {
  if (gpu_frame_bridge_) {
    gpu_frame_bridge_->SetInteropAvailable(false);
  }
  if (context() && gl_renderer_) {
    makeCurrent();
    gl_renderer_->Shutdown();
    doneCurrent();
  }
}

HWND BrowserWidget::NativeParentHandle() const {
  const HWND handle = reinterpret_cast<HWND>(winId());
  DiagnosticLog("BrowserWidget::NativeParentHandle hwnd=" +
                HexValue(reinterpret_cast<uintptr_t>(handle)));
  return handle;
}

BrowserViewRect BrowserWidget::CurrentViewRect() const {
  return ViewRectFromWidgetSize(width(), height());
}

double BrowserWidget::CurrentDeviceScaleFactor() const {
  double scale = 1.0;
  if (windowHandle() && windowHandle()->screen()) {
    scale = windowHandle()->screen()->devicePixelRatio();
  } else {
    scale = devicePixelRatioF();
  }
  return NormalizeDeviceScaleFactor(scale);
}

void BrowserWidget::SetFrame(std::shared_ptr<BrowserFrame> frame) {
  frame_ = std::move(frame);
  DiagnosticLog("BrowserWidget::SetFrame frame=" +
                HexValue(reinterpret_cast<uintptr_t>(frame_.get())));
}

void BrowserWidget::SetGpuFrameBridge(
    std::shared_ptr<GpuFrameBridge> gpu_frame_bridge) {
  if (gpu_frame_bridge_) {
    gpu_frame_bridge_->SetInteropAvailable(false);
  }
  gpu_frame_bridge_ = std::move(gpu_frame_bridge);
  if (gpu_frame_bridge_ && context() && gl_renderer_) {
    gpu_frame_bridge_->SetInteropAvailable(
        gl_renderer_->interop_available());
  }
  DiagnosticLog("BrowserWidget::SetGpuFrameBridge bridge=" +
                HexValue(reinterpret_cast<uintptr_t>(gpu_frame_bridge_.get())));
}

void BrowserWidget::SetRenderStats(std::shared_ptr<RenderStats> stats) {
  render_stats_ = std::move(stats);
}

void BrowserWidget::SetGpuCopyPolicy(std::shared_ptr<GpuCopyPolicy> policy) {
  gpu_copy_policy_ = std::move(policy);
  DiagnosticLog("BrowserWidget::SetGpuCopyPolicy policy=" +
                HexValue(reinterpret_cast<uintptr_t>(gpu_copy_policy_.get())));
}

void BrowserWidget::EmitRenderStats() {
  if (!render_stats_) return;
  // 跨小时时切换 CSV 输出文件（热路径不做时间判断，集中在此秒级定时器）。
  RotateOsrRenderLogIfHourChanged();
  RotateRenderStatsLogIfHourChanged();
  const RenderStatsSnapshot snapshot = render_stats_->SnapshotAndResetWindow();
  if (snapshot.window_frames == 0 && snapshot.window_paint_events == 0) {
    return;
  }
  DiagnosticLog(FormatRenderStatsSummary(snapshot));
  RenderStatsLogWrite(snapshot);
  emit renderStatsUpdated(snapshot);
}

void BrowserWidget::SetResizeCallback(ResizeCallback resize_callback) {
  resize_callback_ = std::move(resize_callback);
  DiagnosticLog("BrowserWidget::SetResizeCallback");
}

void BrowserWidget::SetBrowserService(BrowserService* service) {
  if (browser_service_ && browser_service_ != service) {
    CancelCefDragging();
    browser_service_->SetStartDraggingCallback({});
    browser_service_->SetUpdateDragCursorCallback({});
  }

  browser_service_ = service;
  DiagnosticLog("BrowserWidget::SetBrowserService service=" +
                HexValue(reinterpret_cast<uintptr_t>(service)));

  if (!browser_service_) {
    return;
  }

  QPointer<BrowserWidget> widget_guard(this);
  browser_service_->SetStartDraggingCallback(
      [widget_guard](CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefDragData> drag_data,
                     CefRenderHandler::DragOperationsMask allowed_ops,
                     int screen_x, int screen_y) {
        if (!widget_guard) return false;

        bool handled = false;
        auto start_drag = [&handled, widget_guard, browser, drag_data,
                           allowed_ops, screen_x, screen_y]() {
          if (widget_guard) {
            handled = widget_guard->StartCefDragging(
                browser, drag_data, allowed_ops, screen_x, screen_y);
          }
        };

        if (QThread::currentThread() == widget_guard->thread()) {
          start_drag();
        } else if (widget_guard) {
          QMetaObject::invokeMethod(widget_guard.data(), start_drag,
                                    Qt::BlockingQueuedConnection);
        }
        return handled;
      });

  browser_service_->SetUpdateDragCursorCallback(
      [widget_guard](CefRefPtr<CefBrowser>,
                     CefRenderHandler::DragOperation operation) {
        if (!widget_guard) return;

        auto update_cursor = [widget_guard, operation]() {
          if (widget_guard) {
            widget_guard->UpdateCefDragCursor(operation);
          }
        };

        if (QThread::currentThread() == widget_guard->thread()) {
          update_cursor();
        } else if (widget_guard) {
          QMetaObject::invokeMethod(widget_guard.data(), update_cursor,
                                    Qt::QueuedConnection);
        }
      });
}

void BrowserWidget::SetImeHandler(BrowserImeHandler* handler) {
  ime_handler_ = handler;
  DiagnosticLog("BrowserWidget::SetImeHandler handler=" +
                HexValue(reinterpret_cast<uintptr_t>(handler)));
  if (ime_handler_) {
    // The IMM context belongs to this native browser widget. It must not be
    // rebound to msg->hwnd because Qt may deliver an IME message through an
    // ancestor window.
    ime_handler_->SetWindowHandle(reinterpret_cast<HWND>(winId()));
  }
}

CefRefPtr<CefDragData> BrowserWidget::CreateCefDragData(
    const QMimeData* mime_data) const {
  if (!mime_data) return nullptr;

  CefRefPtr<CefDragData> drag_data = CefDragData::Create();
  bool has_supported_data = false;

  if (mime_data->hasUrls()) {
    for (const QUrl& url : mime_data->urls()) {
      if (url.isLocalFile()) {
        const QString local_file = QDir::toNativeSeparators(url.toLocalFile());
        const QString display_name = QFileInfo(local_file).fileName();
        drag_data->AddFile(local_file.toStdWString(),
                           display_name.toStdWString());
        has_supported_data = true;
      } else if (!has_supported_data) {
        drag_data->SetLinkURL(url.toString().toStdWString());
        drag_data->SetLinkTitle(url.toString().toStdWString());
        has_supported_data = true;
      }
    }
  }

  if (mime_data->hasHtml()) {
    drag_data->SetFragmentHtml(mime_data->html().toStdWString());
    has_supported_data = true;
  }

  if (mime_data->hasText()) {
    drag_data->SetFragmentText(mime_data->text().toStdWString());
    has_supported_data = true;
  }

  return has_supported_data ? drag_data : nullptr;
}

bool BrowserWidget::StartCefDragging(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDragData> drag_data,
    CefRenderHandler::DragOperationsMask allowed_ops,
    int screen_x,
    int screen_y) {
  (void)browser;
  if (!browser_service_ || !drag_data ||
      allowed_ops == DRAG_OPERATION_NONE) {
    DiagnosticLog("BrowserWidget::StartCefDragging rejected has_service=" +
                  std::string(browser_service_ ? "true" : "false") +
                  " has_drag_data=" + (drag_data ? "true" : "false") +
                  " allowed_ops=" + std::to_string(allowed_ops));
    return false;
  }

  CancelCefDragging();

  cef_drag_data_ = drag_data->Clone();
  if (!cef_drag_data_) {
    cef_drag_data_ = drag_data;
  }
  cef_drag_allowed_ops_ = allowed_ops;
  cef_drag_current_op_ = PreferredDragOperation(allowed_ops);
  cef_drag_source_active_ = true;

  QPoint position = mapFromGlobal(QPoint(screen_x, screen_y));
  if (!rect().contains(position) && rect().contains(last_mouse_pos_)) {
    position = last_mouse_pos_;
  }

  DiagnosticLog("BrowserWidget::StartCefDragging screen=" +
                std::to_string(screen_x) + "," + std::to_string(screen_y) +
                " view=" + std::to_string(position.x()) + "," +
                std::to_string(position.y()) +
                " allowed_ops=" + std::to_string(allowed_ops));
  SendCefDragEnter(position, static_cast<int>(QApplication::mouseButtons()),
                   static_cast<int>(QApplication::keyboardModifiers()));
  return true;
}

void BrowserWidget::UpdateCefDragCursor(
    CefRenderHandler::DragOperation operation) {
  cef_drag_current_op_ = operation;
  if (operation == DRAG_OPERATION_NONE) {
    setCursor(Qt::ForbiddenCursor);
  } else if (operation & DRAG_OPERATION_COPY) {
    setCursor(Qt::DragCopyCursor);
  } else if (operation & DRAG_OPERATION_LINK) {
    setCursor(Qt::PointingHandCursor);
  } else if (operation & DRAG_OPERATION_MOVE) {
    setCursor(Qt::ClosedHandCursor);
  } else {
    setCursor(Qt::ClosedHandCursor);
  }
}

void BrowserWidget::SendCefDragEnter(const QPoint& position,
                                     int buttons,
                                     int modifiers) {
  if (!browser_service_ || !cef_drag_data_) return;
  browser_service_->SendDragTargetDragEnter(
      cef_drag_data_, position.x(), position.y(), buttons, modifiers,
      cef_drag_allowed_ops_);
  cef_drag_target_active_ = true;
}

void BrowserWidget::FinishCefDragging(const QPoint& position,
                                      int buttons,
                                      int modifiers,
                                      bool dropped) {
  if (!cef_drag_source_active_) return;

  const bool can_drop = dropped && rect().contains(position) &&
                        cef_drag_current_op_ != DRAG_OPERATION_NONE;
  CefRenderHandler::DragOperation operation =
      can_drop ? cef_drag_current_op_ : DRAG_OPERATION_NONE;

  if (browser_service_) {
    if (cef_drag_target_active_) {
      if (can_drop) {
        browser_service_->SendDragTargetDrop(position.x(), position.y(),
                                            buttons, modifiers);
      } else {
        browser_service_->SendDragTargetDragLeave();
      }
    }
    browser_service_->SendDragSourceEndedAt(position.x(), position.y(),
                                           operation);
    browser_service_->SendDragSourceSystemDragEnded();
  }

  DiagnosticLog("BrowserWidget::FinishCefDragging dropped=" +
                std::string(can_drop ? "true" : "false") +
                " op=" + std::to_string(operation) +
                " pos=" + std::to_string(position.x()) + "," +
                std::to_string(position.y()));

  cef_drag_source_active_ = false;
  cef_drag_target_active_ = false;
  cef_drag_data_ = nullptr;
  cef_drag_allowed_ops_ = DRAG_OPERATION_NONE;
  cef_drag_current_op_ = DRAG_OPERATION_NONE;
  setCursor(Qt::ArrowCursor);
}

void BrowserWidget::CancelCefDragging() {
  if (!cef_drag_source_active_) return;
  if (browser_service_) {
    if (cef_drag_target_active_) {
      browser_service_->SendDragTargetDragLeave();
    }
    browser_service_->SendDragSourceSystemDragEnded();
  }
  cef_drag_source_active_ = false;
  cef_drag_target_active_ = false;
  cef_drag_data_ = nullptr;
  cef_drag_allowed_ops_ = DRAG_OPERATION_NONE;
  cef_drag_current_op_ = DRAG_OPERATION_NONE;
  setCursor(Qt::ArrowCursor);
}

void BrowserWidget::OnImeCompositionRangeChanged(
    const CefRange& selected_range,
    const std::vector<CefRect>& bounds) {
  DiagnosticLog("BrowserWidget::OnImeCompositionRangeChanged selected=" +
                std::to_string(selected_range.from) + "-" +
                std::to_string(selected_range.to) + " bounds=" +
                std::to_string(bounds.size()));
  if (!ime_handler_) return;

  const double scale = CurrentDeviceScaleFactor();
  std::vector<CefRect> device_bounds;
  device_bounds.reserve(bounds.size());
  for (const auto& b : bounds) {
    device_bounds.push_back(CefRect(
        static_cast<int>(b.x * scale),
        static_cast<int>(b.y * scale),
        static_cast<int>(b.width * scale),
        static_cast<int>(b.height * scale)));
  }

  CefRange device_range;
  device_range.from = selected_range.from;
  device_range.to = selected_range.to;

  ime_handler_->UpdateCompositionRange(device_range, device_bounds);
}

void BrowserWidget::SetCefCursor(int cursor_type, HCURSOR cursor_handle) {
  const HWND hwnd = reinterpret_cast<HWND>(winId());
  if (cursor_handle && ::IsWindow(hwnd)) {
    ::SetClassLongPtr(
        hwnd, GCLP_HCURSOR,
        static_cast<LONG_PTR>(reinterpret_cast<uintptr_t>(cursor_handle)));
    ::SetCursor(cursor_handle);
    return;
  }

  switch (cursor_type) {
    case kCursorPointer:
      setCursor(Qt::ArrowCursor);
      break;
    case kCursorCross:
      setCursor(Qt::CrossCursor);
      break;
    case kCursorHand:
      setCursor(Qt::PointingHandCursor);
      break;
    case kCursorIBeam:
      setCursor(Qt::IBeamCursor);
      break;
    case kCursorWait:
      setCursor(Qt::WaitCursor);
      break;
    case kCursorHelp:
      setCursor(Qt::WhatsThisCursor);
      break;
    case kCursorEastResize:
    case kCursorWestResize:
    case kCursorEastWestResize:
      setCursor(Qt::SizeHorCursor);
      break;
    case kCursorNorthResize:
    case kCursorSouthResize:
    case kCursorNorthSouthResize:
      setCursor(Qt::SizeVerCursor);
      break;
    case kCursorNorthEastResize:
    case kCursorSouthWestResize:
      setCursor(Qt::SizeBDiagCursor);
      break;
    case kCursorNorthWestResize:
    case kCursorSouthEastResize:
      setCursor(Qt::SizeFDiagCursor);
      break;
    case kCursorMove:
      setCursor(Qt::SizeAllCursor);
      break;
    case kCursorVerticalText:
      setCursor(Qt::IBeamCursor);
      break;
    case kCursorCell:
      setCursor(Qt::UpArrowCursor);
      break;
    case kCursorContextMenu:
      setCursor(Qt::ArrowCursor);
      break;
    case kCursorAlias:
      setCursor(Qt::PointingHandCursor);
      break;
    case kCursorProgress:
      setCursor(Qt::WaitCursor);
      break;
    case kCursorNoDrop:
      setCursor(Qt::ForbiddenCursor);
      break;
    case kCursorCopy:
      setCursor(Qt::DragCopyCursor);
      break;
    case kCursorNone:
      setCursor(Qt::BlankCursor);
      break;
    case kCursorNotAllowed:
      setCursor(Qt::ForbiddenCursor);
      break;
    case kCursorZoomIn:
    case kCursorZoomOut:
      setCursor(Qt::SizeAllCursor);
      break;
    case kCursorGrab:
      setCursor(Qt::OpenHandCursor);
      break;
    case kCursorGrabbing:
      setCursor(Qt::ClosedHandCursor);
      break;
    default:
      setCursor(Qt::ArrowCursor);
      break;
  }
}

bool BrowserWidget::HandleImeNativeMessage(MSG* windows_message,
                                            qintptr* result) {
  switch (windows_message->message) {
    case WM_INPUTLANGCHANGE:
      DiagnosticLog("BrowserWidget::HandleImeNativeMessage WM_INPUTLANGCHANGE wParam=" +
                    HexValue(static_cast<uintptr_t>(windows_message->wParam)) +
                    " lParam=" +
                    HexValue(static_cast<uintptr_t>(windows_message->lParam)));
      if (ime_handler_) {
        ime_handler_->HandleInputLanguageChange();
      }
      return false;
    case WM_IME_SETCONTEXT:
      DiagnosticLog("BrowserWidget::HandleImeNativeMessage WM_IME_SETCONTEXT wParam=" +
                    HexValue(static_cast<uintptr_t>(windows_message->wParam)) +
                    " lParam=" +
                    HexValue(static_cast<uintptr_t>(windows_message->lParam)));
      if (ime_handler_) {
        ime_handler_->HandleImeSetContext(windows_message->lParam);
      }
      return false;
    case WM_IME_STARTCOMPOSITION:
      DiagnosticLog("BrowserWidget::HandleImeNativeMessage WM_IME_STARTCOMPOSITION");
      is_composing_ = true;
      if (ime_handler_) {
        ime_handler_->HandleImeStartComposition();
      }
      if (result) *result = 0;
      return true;
    case WM_IME_COMPOSITION:
      DiagnosticLog("BrowserWidget::HandleImeNativeMessage WM_IME_COMPOSITION wParam=" +
                    HexValue(static_cast<uintptr_t>(windows_message->wParam)) +
                    " lParam=" +
                    HexValue(static_cast<uintptr_t>(windows_message->lParam)));
      HandleImeCompositionMessage(windows_message->wParam,
                                  windows_message->lParam);
      if (result) *result = 0;
      return true;
    case WM_IME_ENDCOMPOSITION:
      DiagnosticLog("BrowserWidget::HandleImeNativeMessage WM_IME_ENDCOMPOSITION");
      is_composing_ = false;
      if (ime_handler_) {
        ime_handler_->HandleImeEndComposition();
      }
      if (result) *result = 0;
      return true;
    case WM_IME_CHAR: {
      // Some IMM32 input methods submit the selected candidate through
      // WM_IME_CHAR instead of GCS_RESULTSTR. Forward it explicitly because
      // native event filtering prevents Qt from producing a QKeyEvent here.
      const wchar_t ime_char = static_cast<wchar_t>(
          static_cast<uintptr_t>(windows_message->wParam) & 0xFFFFu);
      const std::wstring committed_text(1, ime_char);
      DiagnosticLog("BrowserWidget::HandleImeNativeMessage WM_IME_CHAR commit wParam=" +
                    HexValue(static_cast<uintptr_t>(windows_message->wParam)) +
                    " utf16=" +
                    HexValue(static_cast<uintptr_t>(ime_char)) +
                    " has_browser_service=" +
                    (browser_service_ ? std::string("true")
                                      : std::string("false")));
      if (browser_service_) {
        const CefRange invalid_range(UINT32_MAX, UINT32_MAX);
        browser_service_->ImeCommitText(committed_text, invalid_range, 0);
      }
      is_composing_ = false;
      if (ime_handler_) {
        ime_handler_->ResetComposition();
      }
      if (result) *result = 0;
      return true;
    }
    default:
      return false;
  }
}

void BrowserWidget::ScheduleFrameUpdate(
    const std::vector<BrowserViewRect>& dirty_rects) {
  static std::atomic<int> schedule_count{0};
  if (ShouldDiagnosticLog(schedule_count, 40, 100)) {
    std::ostringstream stream;
    stream << "BrowserWidget::ScheduleFrameUpdate dirty_count="
           << dirty_rects.size();
    if (!dirty_rects.empty()) {
      const auto& first = dirty_rects.front();
      stream << " first_dirty=" << first.x << "," << first.y << " "
             << first.width << "x" << first.height;
    }
    DiagnosticLog(stream.str());
  }
  if (dirty_rects.empty()) {
    QMetaObject::invokeMethod(this, [this]() { update(); },
                              Qt::QueuedConnection);
    return;
  }
  QRegion region;
  for (const auto& r : dirty_rects) {
    region += QRect(r.x, r.y, r.width, r.height);
  }
  QMetaObject::invokeMethod(this, [this, region]() { update(region); },
                            Qt::QueuedConnection);
}

void BrowserWidget::resizeEvent(QResizeEvent* event) {
  QOpenGLWidget::resizeEvent(event);
  static std::atomic<int> resize_event_count{0};
  if (ShouldDiagnosticLog(resize_event_count, 20, 50)) {
    std::ostringstream stream;
    stream << "BrowserWidget::resizeEvent widget=" << width() << "x"
           << height() << " scale=" << CurrentDeviceScaleFactor()
           << " has_resize_callback="
           << (resize_callback_ ? "true" : "false");
    DiagnosticLog(stream.str());
  }
  if (resize_callback_) {
    resize_callback_(CurrentViewRect(), CurrentDeviceScaleFactor());
  }
}

void BrowserWidget::initializeGL() {
  const bool initialized = gl_renderer_ && gl_renderer_->Initialize();
  if (gpu_frame_bridge_) {
    gpu_frame_bridge_->SetInteropAvailable(
        initialized && gl_renderer_->interop_available());
  }
  DiagnosticLog("BrowserWidget::initializeGL initialized=" +
                std::string(initialized ? "true" : "false") +
                " wgl_dx_interop=" +
                (initialized && gl_renderer_->interop_available()
                     ? "available"
                     : "unavailable"));
}

void BrowserWidget::paintGL() {
  static std::atomic<int> paint_event_count{0};
  RenderStats* const stats = render_stats_.get();
  if (stats) {
    stats->OnPaintEventBegin();
    stats->OnSnapshotBegin();
  }

  BrowserFrameSnapshot cpu_snapshot;
  if (frame_) {
    cpu_snapshot = frame_->Snapshot();
  }
  GpuFrameSnapshot view_gpu;
  GpuFrameSnapshot popup_gpu;
  if (gpu_frame_bridge_) {
    view_gpu = gpu_frame_bridge_->Snapshot(GpuFrameKind::kView);
    popup_gpu = gpu_frame_bridge_->Snapshot(GpuFrameKind::kPopup);
  }
  if (stats) {
    stats->OnSnapshotDone();
  }

  const double scale = CurrentDeviceScaleFactor();
  const int viewport_width =
      std::max(1, static_cast<int>(std::lround(width() * scale)));
  const int viewport_height =
      std::max(1, static_cast<int>(std::lround(height() * scale)));
  cpu_snapshot.popup_rect.x =
      static_cast<int>(std::lround(cpu_snapshot.popup_rect.x * scale));
  cpu_snapshot.popup_rect.y =
      static_cast<int>(std::lround(cpu_snapshot.popup_rect.y * scale));
  cpu_snapshot.popup_rect.width =
      static_cast<int>(std::lround(cpu_snapshot.popup_rect.width * scale));
  cpu_snapshot.popup_rect.height =
      static_cast<int>(std::lround(cpu_snapshot.popup_rect.height * scale));

  if (stats) {
    stats->OnDrawImageBegin();
  }
  const GpuPresentResult result =
      gl_renderer_
          ? gl_renderer_->Render(view_gpu, popup_gpu, cpu_snapshot,
                                 viewport_width, viewport_height)
          : GpuPresentResult{};
  const GpuPresentPath path = result.path;
  if (stats) {
    stats->OnDrawImageDone();
  }

  // 按资源把 GPU 呈现结果上报降级策略：成功清除失败态，失败进入退避。
  if (gpu_copy_policy_) {
    if (result.view_attempted_gpu) {
      gpu_copy_policy_->ReportPresentOutcome(
          GpuFrameKind::kView, result.view_gpu_succeeded,
          result.view_gpu_succeeded ? "" : result.view_failure_stage);
    }
    if (result.popup_attempted_gpu) {
      gpu_copy_policy_->ReportPresentOutcome(
          GpuFrameKind::kPopup, result.popup_gpu_succeeded,
          result.popup_gpu_succeeded ? "" : result.popup_failure_stage);
    }
  }
  if (stats) {
    if (result.view_attempted_gpu) {
      if (result.view_gpu_succeeded &&
          path == GpuPresentPath::kWglDxInterop &&
          view_gpu.publication.frame_generation !=
              last_presented_gpu_frame_generation_) {
        last_presented_gpu_frame_generation_ =
            view_gpu.publication.frame_generation;
        stats->OnGpuFramePresented(GpuFrameKind::kView, path, "");
      } else if (!result.view_gpu_succeeded) {
        stats->OnGpuFramePresented(GpuFrameKind::kView,
                                   GpuPresentPath::kWglDxInterop,
                                   result.view_failure_stage);
      }
    } else if (path == GpuPresentPath::kCpuGlUpload ||
               path == GpuPresentPath::kQImageFallback) {
      stats->OnGpuFramePresented(GpuFrameKind::kView, path, "");
    }
    if (result.popup_attempted_gpu) {
      stats->OnGpuFramePresented(GpuFrameKind::kPopup,
                                 GpuPresentPath::kWglDxInterop,
                                 result.popup_gpu_succeeded
                                     ? ""
                                     : result.popup_failure_stage);
    }
    stats->OnPaintEventEnd();
  }

  if (ShouldDiagnosticLog(paint_event_count, 40, 100)) {
    std::ostringstream stream;
    stream << "BrowserWidget::paintGL path=" << GpuPresentPathName(path)
           << " widget=" << width() << "x" << height()
           << " viewport=" << viewport_width << "x" << viewport_height
           << " gpu_frame=" << view_gpu.publication.frame_generation
           << " cpu_view=" << (cpu_snapshot.has_view ? "true" : "false")
           << " popup_visible="
           << (cpu_snapshot.popup_visible ? "true" : "false");
    DiagnosticLog(stream.str());
  }
}

void BrowserWidget::mousePressEvent(QMouseEvent* event) {
  last_mouse_pos_ = event->pos();
  if (browser_service_) {
    browser_service_->SendMouseClickEvent(
        event->pos().x(), event->pos().y(),
        static_cast<int>(event->button()),
        static_cast<int>(event->buttons()),
        false, 1, static_cast<int>(event->modifiers()));
  }
}

void BrowserWidget::mouseReleaseEvent(QMouseEvent* event) {
  last_mouse_pos_ = event->pos();
  if (cef_drag_source_active_) {
    FinishCefDragging(event->pos(), static_cast<int>(event->buttons()),
                      static_cast<int>(event->modifiers()), true);
    event->accept();
    return;
  }

  if (browser_service_) {
    browser_service_->SendMouseClickEvent(
        event->pos().x(), event->pos().y(),
        static_cast<int>(event->button()),
        static_cast<int>(event->buttons()),
        true, 1, static_cast<int>(event->modifiers()));
  }
}

void BrowserWidget::mouseMoveEvent(QMouseEvent* event) {
  last_mouse_pos_ = event->pos();
  if (cef_drag_source_active_) {
    if (rect().contains(event->pos())) {
      if (!cef_drag_target_active_) {
        SendCefDragEnter(event->pos(), static_cast<int>(event->buttons()),
                         static_cast<int>(event->modifiers()));
      } else if (browser_service_) {
        browser_service_->SendDragTargetDragOver(
            event->pos().x(), event->pos().y(),
            static_cast<int>(event->buttons()),
            static_cast<int>(event->modifiers()), cef_drag_allowed_ops_);
      }
    } else if (browser_service_ && cef_drag_target_active_) {
      browser_service_->SendDragTargetDragLeave();
      cef_drag_target_active_ = false;
    }
    event->accept();
    return;
  }

  if (browser_service_) {
    browser_service_->SendMouseMoveEvent(
        event->pos().x(), event->pos().y(),
        static_cast<int>(event->buttons()),
        static_cast<int>(event->modifiers()), false);
  }
}

void BrowserWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (browser_service_) {
    browser_service_->SendMouseClickEvent(
        event->pos().x(), event->pos().y(),
        static_cast<int>(event->button()),
        static_cast<int>(event->buttons()),
        false, 2, static_cast<int>(event->modifiers()));
  }
}

void BrowserWidget::wheelEvent(QWheelEvent* event) {
  if (!browser_service_) return;

  int delta_x = 0;
  int delta_y = 0;

  QPoint pixel_delta = event->pixelDelta();
  if (!pixel_delta.isNull()) {
    delta_x = pixel_delta.x();
    delta_y = pixel_delta.y();
  } else {
    QPoint angle_delta = event->angleDelta();
    delta_x = angle_delta.x();
    delta_y = angle_delta.y();
  }

  const QPoint position = event->position().toPoint();
  browser_service_->SendMouseWheelEvent(
      position.x(), position.y(),
      static_cast<int>(event->buttons()),
      static_cast<int>(event->modifiers()), delta_x, delta_y);
}

void BrowserWidget::contextMenuEvent(QContextMenuEvent* event) {
  // 不再无条件弹菜单；宿主菜单改由 CEF OnBeforeContextMenu 驱动，
  // 避免与网页自定义右键菜单重叠。
  last_context_menu_pos_ = event->globalPos();
  event->accept();
}

void BrowserWidget::RequestContextMenu(int view_x, int view_y) {
  // Windows 上 contextMenuEvent（右键抬起）先于 OnBeforeContextMenu 触发，
  // 记录的全局坐标精确无缩放歧义；CEF 视图坐标作回退。
  const QPoint global_pos = last_context_menu_pos_.isNull()
      ? mapToGlobal(QPoint(view_x, view_y))
      : last_context_menu_pos_;
  // CEF 回调运行在消息泵内，延迟一个事件循环再弹菜单，避免在
  // OnBeforeContextMenu 栈内 QMenu::exec() 嵌套循环导致 CEF 重入。
  QTimer::singleShot(0, this, [this, global_pos]() {
    emit contextMenuRequested(global_pos);
  });
}

void BrowserWidget::leaveEvent(QEvent* event) {
  QWidget::leaveEvent(event);
  if (cef_drag_source_active_) {
    if (browser_service_ && cef_drag_target_active_) {
      browser_service_->SendDragTargetDragLeave();
      cef_drag_target_active_ = false;
    }
    return;
  }

  if (browser_service_) {
    browser_service_->SendMouseMoveEvent(
        last_mouse_pos_.x(), last_mouse_pos_.y(),
        0, 0, true);
  }
}

void BrowserWidget::keyPressEvent(QKeyEvent* event) {
  if (!browser_service_) {
    QWidget::keyPressEvent(event);
    return;
  }

  // During IME composition, skip SendCharEvent to avoid duplicate text
  if (is_composing_) {
    DiagnosticLog("BrowserWidget::keyPressEvent skipped during IME composition");
    event->accept();
    return;
  }

  const int virtual_key = static_cast<int>(event->nativeVirtualKey());
  const int scan_code = static_cast<int>(event->nativeScanCode());
  const int modifiers = static_cast<int>(event->modifiers());
  const bool is_keypad = (event->modifiers() & Qt::KeypadModifier) != 0;
  const uint32_t native_key_code = BuildWindowsNativeKeyCode(
      virtual_key, scan_code, event->isAutoRepeat(), false);
  const bool is_tab = (event->key() == Qt::Key_Tab ||
                       event->key() == Qt::Key_Backtab);
  const QByteArray text_utf8 = event->text().toUtf8();
  const std::string text_for_log(text_utf8.constData(),
                                 static_cast<size_t>(text_utf8.size()));

  DiagnosticLog("BrowserWidget::keyPressEvent key=" +
                std::to_string(event->key()) +
                " vk=" + std::to_string(virtual_key) +
                " scan=" + std::to_string(scan_code) +
                " modifiers=" + std::to_string(modifiers) +
                " tab=" + (is_tab ? "true" : "false") +
                " auto=" + (event->isAutoRepeat() ? "true" : "false") +
                " text=[" + text_for_log + "]");

  browser_service_->SendRawKeyDown(virtual_key, native_key_code, modifiers,
                                   is_keypad);
  DiagnosticLog("BrowserWidget::keyPressEvent rawKeyDown done");

  // Char events for printable text (skip Tab/Backtab and auto-repeat)
  if (!is_tab && !event->text().isEmpty() && !event->isAutoRepeat()) {
    DiagnosticLog("BrowserWidget::keyPressEvent entering CHAR path");
    const QString text = event->text();
    for (int index = 0; index < text.size(); ++index) {
      const char16_t ch = static_cast<char16_t>(text.at(index).unicode());
      DiagnosticLog("BrowserWidget::keyPressEvent sending char=" +
                    std::to_string(static_cast<int>(ch)));
      browser_service_->SendCharEvent(native_key_code, modifiers, ch);
    }
    DiagnosticLog("BrowserWidget::keyPressEvent CHAR path done");
  }
  event->accept();
}

void BrowserWidget::keyReleaseEvent(QKeyEvent* event) {
  if (!browser_service_) {
    QWidget::keyReleaseEvent(event);
    return;
  }

  const int virtual_key = static_cast<int>(event->nativeVirtualKey());
  const int scan_code = static_cast<int>(event->nativeScanCode());
  const int modifiers = static_cast<int>(event->modifiers());
  const bool is_keypad = (event->modifiers() & Qt::KeypadModifier) != 0;
  const uint32_t native_key_code =
      BuildWindowsNativeKeyCode(virtual_key, scan_code, true, true);

  browser_service_->SendKeyUp(virtual_key, native_key_code, modifiers,
                              is_keypad);
  event->accept();
}

void BrowserWidget::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  DiagnosticLog("BrowserWidget::focusInEvent");
  if (browser_service_) {
    browser_service_->SetBrowserFocus(true);
  }
}

void BrowserWidget::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  DiagnosticLog("BrowserWidget::focusOutEvent composing=" +
                std::string(is_composing_ ? "true" : "false"));
  CancelCefDragging();
  if (browser_service_) {
    browser_service_->SetBrowserFocus(false);
  }
  // Cancel composition when losing focus
  if (is_composing_ && ime_handler_) {
    is_composing_ = false;
    ime_handler_->HandleImeEndComposition();
    if (browser_service_) {
      browser_service_->ImeCancelComposition();
    }
  }
}

void BrowserWidget::dragEnterEvent(QDragEnterEvent* event) {
  if (!browser_service_) {
    event->ignore();
    return;
  }

  CefRefPtr<CefDragData> drag_data = CreateCefDragData(event->mimeData());
  if (!drag_data) {
    DiagnosticLog("BrowserWidget::dragEnterEvent ignored unsupported mime data");
    event->ignore();
    return;
  }

  const QPoint position = event->position().toPoint();
  DiagnosticLog("BrowserWidget::dragEnterEvent x=" +
                std::to_string(position.x()) + " y=" +
                std::to_string(position.y()));
  browser_service_->SendDragTargetDragEnter(
      drag_data, position.x(), position.y(),
      static_cast<int>(event->mouseButtons()),
      static_cast<int>(event->keyboardModifiers()));
  drag_active_ = true;
  event->acceptProposedAction();
}

void BrowserWidget::dragMoveEvent(QDragMoveEvent* event) {
  if (!browser_service_ || !drag_active_) {
    event->ignore();
    return;
  }

  const QPoint position = event->position().toPoint();
  browser_service_->SendDragTargetDragOver(
      position.x(), position.y(), static_cast<int>(event->mouseButtons()),
      static_cast<int>(event->keyboardModifiers()));
  event->acceptProposedAction();
}

void BrowserWidget::dragLeaveEvent(QDragLeaveEvent* event) {
  if (browser_service_ && drag_active_) {
    browser_service_->SendDragTargetDragLeave();
  }
  drag_active_ = false;
  event->accept();
}

void BrowserWidget::dropEvent(QDropEvent* event) {
  if (!browser_service_) {
    event->ignore();
    return;
  }

  const QPoint position = event->position().toPoint();
  if (!drag_active_) {
    CefRefPtr<CefDragData> drag_data = CreateCefDragData(event->mimeData());
    if (!drag_data) {
      event->ignore();
      return;
    }
    browser_service_->SendDragTargetDragEnter(
        drag_data, position.x(), position.y(),
        static_cast<int>(event->mouseButtons()),
        static_cast<int>(event->keyboardModifiers()));
  }

  DiagnosticLog("BrowserWidget::dropEvent x=" +
                std::to_string(position.x()) + " y=" +
                std::to_string(position.y()));
  browser_service_->SendDragTargetDrop(
      position.x(), position.y(), static_cast<int>(event->mouseButtons()),
      static_cast<int>(event->keyboardModifiers()));
  drag_active_ = false;
  event->acceptProposedAction();
}

}  // namespace offscreen
