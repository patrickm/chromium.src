// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/touch/touch_device.h"
#include "base/win/windows_version.h"
#include <windows.h>

namespace ui {

bool IsTouchDevicePresent() {
  int value = GetSystemMetrics(SM_DIGITIZER);
  return (value & NID_READY) &&
      ((value & NID_INTEGRATED_TOUCH) || (value & NID_EXTERNAL_TOUCH));
}

int MaxTouchPoints() {
  if (!IsTouchDevicePresent())
    return 0;

  return GetSystemMetrics(SM_MAXIMUMTOUCHES);
}

int GetAvailablePointerTypes() {
  // TODO(mustaq): Replace the stub below
  return POINTER_TYPE_NONE;
}

PointerType GetPrimaryPointerType() {
  // TODO(mustaq): Replace the stub below
  return POINTER_TYPE_NONE;
}

int GetAvailableHoverTypes() {
  // TODO(mustaq): Replace the stub below
  return HOVER_TYPE_NONE;
}

HoverType GetPrimaryHoverType() {
  // TODO(mustaq): Replace the stub below
  return HOVER_TYPE_NONE;
}

}  // namespace ui
