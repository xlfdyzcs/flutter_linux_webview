// Copyright (c) 2023 ACCESS CO., LTD. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of ACCESS CO., LTD. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "flutter_webview_handler.h"

#include <GL/gl.h>

#include <iostream>
#include <sstream>
#include <string>

#include "flutter_linux_webview/flutter_webview_types.h"
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "subprocess/src/flutter_webview_process_messages.h"

FlutterWebviewHandler::FlutterWebviewHandler(
    WebviewId webview_id,
    const WebviewCreationParams& params,
    const std::function<void(WebviewId webview_id,
                             CefRefPtr<CefBrowser> browser)>& on_after_created,
    const std::function<void()>& on_browser_ready,
    const std::function<void(WebviewId webview_id,
                             CefRefPtr<CefBrowser> browser)>& on_before_close)
    : on_paint_begin_(params.on_paint_begin),
      on_paint_end_(params.on_paint_end),
      on_page_started_(params.on_page_started),
      on_page_finished_(params.on_page_finished),
      on_progress_(params.on_progress),
      on_web_resource_error_(params.on_web_resource_error),
      on_javascript_result_(params.on_javascript_result),
      on_after_created_(on_after_created),
      on_browser_ready_(on_browser_ready),
      on_before_close_(on_before_close),
      close_browser_cb_(nullptr),
      webview_id_(webview_id),
      browser_state_(BrowserState::kBeforeCreated),
      browser_(nullptr),
      renderer_(params.native_texture_id),
      webview_width_(params.width),
      webview_height_(params.height) {
  // Ensure that width and height are greater than 0
  if (webview_width_ <= 0 || webview_height_ <= 0) {
    if (webview_width_ <= 0)
      webview_width_ = 1;
    if (webview_height_ <= 0)
      webview_height_ = 1;
    LOG(WARNING) << __FUNCTION__ << ": (" << params.width << ", "
                 << params.height
                 << ") was given. Width and height must be greater than 0. "
                 << "(" << webview_width_ << ", " << webview_height_
                 << ") is used instead.";
  }
}

bool FlutterWebviewHandler::OnBeforePopup(
    CefRefPtr<CefBrowser> parentBrowser,
    const CefPopupFeatures& popupFeatures,
    CefWindowInfo& windowInfo,
    const CefString& url,
    CefRefPtr<CefClient>& client,
    CefBrowserSettings& settings) {
  CEF_REQUIRE_UI_THREAD();

  VLOG(1) << __func__ << ": Loading popup in the main window: url="
          << url.ToString().c_str();
  if (!url.empty()) {
    parentBrowser->GetMainFrame()->LoadURL(url);
  }

  // Block popup
  return true;
}

void FlutterWebviewHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  browser_ = browser;
  browser_state_ = BrowserState::kCreated;

  on_after_created_(webview_id_, browser);
}

void FlutterWebviewHandler::CloseBrowser(
    const std::function<void()>& close_browser_cb) {
  CEF_REQUIRE_UI_THREAD();
  VLOG(1) << __func__ << ": browser_=" << browser_;

  close_browser_cb_ = close_browser_cb;
  browser_state_ = BrowserState::kClosing;
  browser_->GetHost()->CloseBrowser(/* force_close= */ false);
  // Then, DoClose (overrides CefLifeSpanHandler::DoClose) will be called
}

bool FlutterWebviewHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  VLOG(1) << __func__;

  if (browser_state_ != BrowserState::kClosing) {
    LOG(WARNING) << __func__
                 << ": Closing a browser by any way other than calling "
                    "CloseBrowser is not allowed.";
    // Deny window.close();
    return true;
  }

  // Allow the close. For windowed browsers this will result in the OS close
  // event being sent.
  return false;
}

void FlutterWebviewHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  VLOG(1) << __func__;

  browser_ = nullptr;
  on_before_close_(webview_id_, browser);

  browser_state_ = BrowserState::kClosed;

  if (close_browser_cb_) {
    close_browser_cb_();
  }
}

bool FlutterWebviewHandler::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  VLOG(1) << __func__ << ": webview_id_=" << webview_id_
          << ": Message received from the renderer!!: " << message->GetName();

  // Check the message name.
  const std::string& message_name = message->GetName();
  if (message_name ==
      flutter_webview_process_messages::kFrameHostMsg_RunJavascriptResponse) {
    int js_run_id;
    bool was_executed;
    bool is_exception;
    std::string js_result;
    bool is_undefined;
    flutter_webview_process_messages::Read_FrameHostMsg_RunJavascriptResponse(
        message, &js_run_id, &was_executed, &is_exception, &js_result,
        &is_undefined);

    VLOG(1) << "The browser process received js result";
    on_javascript_result_(webview_id_, js_run_id, was_executed, is_exception,
                          js_result, is_undefined);
    return true;
  }
  return false;
}

void FlutterWebviewHandler::SetWebviewSize(int width, int height) {
  CEF_REQUIRE_UI_THREAD();

  if (width <= 0 || height <= 0) {
    LOG(ERROR) << __func__ << ": width and height must be greater than 0.";
    return;
  }

  webview_width_ = width;
  webview_height_ = height;
}

void FlutterWebviewHandler::OnLoadingProgressChange(
    CefRefPtr<CefBrowser> browser,
    double progress) {
  CEF_REQUIRE_UI_THREAD();
  VLOG(1) << __func__ << ": progress=" << progress;

  // progress ranges from 0.0 to 1.0
  on_progress_(webview_id_, static_cast<int>(progress * 100));
}

void FlutterWebviewHandler::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        TransitionType transition_type) {
  CEF_REQUIRE_UI_THREAD();
  VLOG(1) << __func__ << ": frame->IsMain()=" << frame->IsMain()
          << ", frame->GetURL()=" << frame->GetURL();

  if (frame->IsMain()) {
    if (browser_state_ == BrowserState::kCreated) {
      browser_state_ = BrowserState::kReady;
      on_browser_ready_();
    }
    on_page_started_(webview_id_, frame->GetURL().ToString());
  }
}

void FlutterWebviewHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      int httpStatusCode) {
  CEF_REQUIRE_UI_THREAD();
  VLOG(1) << __func__ << ": frame->IsMain()=" << frame->IsMain()
          << ", frame->GetURL()=" << frame->GetURL()
          << ", httpStatusCode=" << httpStatusCode;

  if (frame->IsMain()) {
    on_page_finished_(webview_id_, frame->GetURL().ToString());
  }
}

void FlutterWebviewHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        ErrorCode errorCode,
                                        const CefString& errorText,
                                        const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();
  VLOG(1) << __func__ << ": frame->IsMain()=" << frame->IsMain()
          << ", frame->GetURL()=" << frame->GetURL()
          << ", errorCode=" << errorCode << ", errorText=" << errorText
          << ", failedUrl=" << failedUrl;

  if (browser_state_ == BrowserState::kCreated) {
    browser_state_ = BrowserState::kReady;
    on_browser_ready_();
  }

  if (frame->IsMain()) {
    on_web_resource_error_(webview_id_, errorCode, errorText.ToString(),
                           failedUrl.ToString());
  }
}

void FlutterWebviewHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                        CefRect& rect) {
  CEF_REQUIRE_UI_THREAD();

  // After the webview controller resize the webview size, the browser calls
  // this method to get the new size for OSR.

  rect.width = webview_width_;
  rect.height = webview_height_;
}

void FlutterWebviewHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                    PaintElementType type,
                                    const RectList& dirtyRects,
                                    const void* buffer,
                                    int width,
                                    int height) {
  CEF_REQUIRE_UI_THREAD();

  on_paint_begin_(webview_id_);

  renderer_.OnPaint(browser, type, dirtyRects, buffer, width, height);
  if (type == PET_VIEW && !renderer_.popup_rect().IsEmpty()) {
    browser->GetHost()->Invalidate(PET_POPUP);
  }

  on_paint_end_(webview_id_);
}

void FlutterWebviewHandler::OnPopupShow(CefRefPtr<CefBrowser> browser,
                                        bool show) {
  CEF_REQUIRE_UI_THREAD();

  renderer_.OnPopupShow(browser, show);
}

void FlutterWebviewHandler::OnPopupSize(CefRefPtr<CefBrowser> browser,
                                        const CefRect& rect) {
  CEF_REQUIRE_UI_THREAD();

  renderer_.OnPopupSize(browser, rect);
}
