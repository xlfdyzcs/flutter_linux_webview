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

// This file is based on
// https://bitbucket.org/chromiumembedded/cef/src/4664/tests/cefclient/browser/osr_renderer.cc
// for off-screen rendering.

// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the name Chromium Embedded
// Framework nor the names of its contributors may be used to endorse
// or promote products derived from this software without specific prior
// written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "flutter_webview_osr_renderer.h"

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

// DCHECK on gl errors.
#if DCHECK_IS_ON()
#define VERIFY_NO_ERROR                                                      \
  {                                                                          \
    int _gl_error = glGetError();                                            \
    DCHECK(_gl_error == GL_NO_ERROR) << "glGetError returned " << _gl_error; \
  }
#else
#define VERIFY_NO_ERROR
#endif

FlutterWebviewOsrRenderer::FlutterWebviewOsrRenderer(GLuint native_texture_id)
    : native_texture_id_(native_texture_id) {}

void FlutterWebviewOsrRenderer::OnPopupShow(CefRefPtr<CefBrowser> browser,
                                            bool show) {
  if (!show) {
    // Clear the popup rectangle.
    ClearPopupRects();
  }
}

void FlutterWebviewOsrRenderer::OnPopupSize(CefRefPtr<CefBrowser> browser,
                                            const CefRect& rect) {
  if (rect.width <= 0 || rect.height <= 0)
    return;
  original_popup_rect_ = rect;
  popup_rect_ = GetPopupRectInWebView(original_popup_rect_);
}

CefRect FlutterWebviewOsrRenderer::GetPopupRectInWebView(
    const CefRect& original_rect) {
  CefRect rc(original_rect);
  // if x or y are negative, move them to 0.
  if (rc.x < 0)
    rc.x = 0;
  if (rc.y < 0)
    rc.y = 0;
  // if popup goes outside the view, try to reposition origin
  if (rc.x + rc.width > view_width_)
    rc.x = view_width_ - rc.width;
  if (rc.y + rc.height > view_height_)
    rc.y = view_height_ - rc.height;
  // if x or y became negative, move them to 0 again.
  if (rc.x < 0)
    rc.x = 0;
  if (rc.y < 0)
    rc.y = 0;
  return rc;
}

void FlutterWebviewOsrRenderer::ClearPopupRects() {
  popup_rect_.Set(0, 0, 0, 0);
  original_popup_rect_.Set(0, 0, 0, 0);
}

void FlutterWebviewOsrRenderer::OnPaint(
    CefRefPtr<CefBrowser> browser,
    CefRenderHandler::PaintElementType type,
    const CefRenderHandler::RectList& dirtyRects,
    const void* buffer,
    int width,
    int height) {
  DCHECK_NE(native_texture_id_, 0U);
  glBindTexture(GL_TEXTURE_2D, native_texture_id_);
  VERIFY_NO_ERROR;

  if (type == PET_VIEW) {
    int old_width = view_width_;
    int old_height = view_height_;

    view_width_ = width;
    view_height_ = height;

    glPixelStorei(GL_UNPACK_ROW_LENGTH, view_width_);
    VERIFY_NO_ERROR;

    if (old_width != view_width_ || old_height != view_height_ ||
        (dirtyRects.size() == 1 &&
         dirtyRects[0] == CefRect(0, 0, view_width_, view_height_))) {
      // Update/resize the whole texture.
      glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
      VERIFY_NO_ERROR;
      glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
      VERIFY_NO_ERROR;
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, view_width_, view_height_, 0,
                   GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buffer);
      VERIFY_NO_ERROR;
    } else {
      // Update just the dirty rectangles.
      CefRenderHandler::RectList::const_iterator i = dirtyRects.begin();
      for (; i != dirtyRects.end(); ++i) {
        const CefRect& rect = *i;
        DCHECK(rect.x + rect.width <= view_width_);
        DCHECK(rect.y + rect.height <= view_height_);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, rect.x);
        VERIFY_NO_ERROR;
        glPixelStorei(GL_UNPACK_SKIP_ROWS, rect.y);
        VERIFY_NO_ERROR;
        glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, rect.y, rect.width,
                        rect.height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                        buffer);
        VERIFY_NO_ERROR;
      }
    }
  } else if (type == PET_POPUP && popup_rect_.width > 0 &&
             popup_rect_.height > 0) {
    int skip_pixels = 0, x = popup_rect_.x;
    int skip_rows = 0, y = popup_rect_.y;
    int w = width;
    int h = height;

    // Adjust the popup to fit inside the view.
    if (x < 0) {
      skip_pixels = -x;
      x = 0;
    }
    if (y < 0) {
      skip_rows = -y;
      y = 0;
    }
    if (x + w > view_width_)
      w -= x + w - view_width_;
    if (y + h > view_height_)
      h -= y + h - view_height_;

    // Update the popup rectangle.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
    VERIFY_NO_ERROR;
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
    VERIFY_NO_ERROR;
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
    VERIFY_NO_ERROR;
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_BGRA,
                    GL_UNSIGNED_INT_8_8_8_8_REV, buffer);
    VERIFY_NO_ERROR;
  }
}
