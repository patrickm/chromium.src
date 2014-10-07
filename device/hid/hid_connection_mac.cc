// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/hid/hid_connection_mac.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/mac/foundation_util.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/thread_task_runner_handle.h"
#include "device/hid/hid_connection_mac.h"

namespace device {

HidConnectionMac::HidConnectionMac(
    HidDeviceInfo device_info,
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner)
    : HidConnection(device_info),
      device_(device_info.device_id, base::scoped_policy::RETAIN),
      ui_task_runner_(ui_task_runner) {
  task_runner_ = base::ThreadTaskRunnerHandle::Get();
  DCHECK(task_runner_.get());
  DCHECK(device_.get());

  size_t expected_report_size = device_info.max_input_report_size;
  if (device_info.has_report_id) {
    expected_report_size++;
  }
  inbound_buffer_.resize(expected_report_size);

  ui_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&HidConnectionMac::StartInputReportCallbacks, this));
}

HidConnectionMac::~HidConnectionMac() {
}

void HidConnectionMac::PlatformClose() {
  // To avoid a race between input reports delivered on the UI thread and
  // closing this connection from its native thread the callback must be
  // unregistered on the UI thread. It is safe to pass the this pointer
  // unretained because StartInputReportCallbacks still holds a reference to it.
  // We don't want this closure, which will be release on the UI thread, to be
  // the final reference. StopInputReportCallbacks will use ReleaseSoon() to
  // release this back on the native thread.
  ui_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&HidConnectionMac::StopInputReportCallbacks,
                 base::Unretained(this)));

  while (!pending_reads_.empty()) {
    pending_reads_.front().callback.Run(false, NULL, 0);
    pending_reads_.pop();
  }
}

void HidConnectionMac::PlatformRead(const ReadCallback& callback) {
  DCHECK(thread_checker().CalledOnValidThread());
  PendingHidRead pending_read;
  pending_read.callback = callback;
  pending_reads_.push(pending_read);
  ProcessReadQueue();
}

void HidConnectionMac::PlatformWrite(scoped_refptr<net::IOBuffer> buffer,
                                     size_t size,
                                     const WriteCallback& callback) {
  WriteReport(kIOHIDReportTypeOutput, buffer, size, callback);
}

void HidConnectionMac::PlatformGetFeatureReport(uint8_t report_id,
                                                const ReadCallback& callback) {
  scoped_refptr<net::IOBufferWithSize> buffer(
      new net::IOBufferWithSize(device_info().max_feature_report_size));
  CFIndex report_size = buffer->size();

  // The IOHIDDevice object is shared with the UI thread and so this function
  // should probably be called there but it may block and the asynchronous
  // version is NOT IMPLEMENTED. I've examined the open source implementation
  // of this function and believe it to be thread-safe.
  IOReturn result =
      IOHIDDeviceGetReport(device_.get(),
                           kIOHIDReportTypeFeature,
                           report_id,
                           reinterpret_cast<uint8_t*>(buffer->data()),
                           &report_size);
  if (result == kIOReturnSuccess) {
    callback.Run(true, buffer, report_size);
  } else {
    VLOG(1) << "Failed to get feature report: " << base::StringPrintf("0x%08x",
                                                                      result);
    callback.Run(false, NULL, 0);
  }
}

void HidConnectionMac::PlatformSendFeatureReport(
    scoped_refptr<net::IOBuffer> buffer,
    size_t size,
    const WriteCallback& callback) {
  WriteReport(kIOHIDReportTypeFeature, buffer, size, callback);
}

void HidConnectionMac::StartInputReportCallbacks() {
  DCHECK(ui_task_runner_->BelongsToCurrentThread());
  if (inbound_buffer_.size() > 0) {
    AddRef();  // Hold a reference to this while this callback is registered.
    IOHIDDeviceRegisterInputReportCallback(
        device_.get(),
        &inbound_buffer_[0],
        inbound_buffer_.size(),
        &HidConnectionMac::InputReportCallback,
        this);
  }
}

void HidConnectionMac::StopInputReportCallbacks() {
  DCHECK(ui_task_runner_->BelongsToCurrentThread());
  if (inbound_buffer_.size() > 0) {
    IOHIDDeviceRegisterInputReportCallback(
        device_.get(), &inbound_buffer_[0], inbound_buffer_.size(), NULL, this);
    // Release the reference to this taken by StartInputReportCallbacks but do
    // so on the right thread as this is likely the final reference.
    task_runner_->ReleaseSoon(FROM_HERE, this);
  }
}

// static
void HidConnectionMac::InputReportCallback(void* context,
                                           IOReturn result,
                                           void* sender,
                                           IOHIDReportType type,
                                           uint32_t report_id,
                                           uint8_t* report_bytes,
                                           CFIndex report_length) {
  HidConnectionMac* connection = static_cast<HidConnectionMac*>(context);
  DCHECK(connection->ui_task_runner_->BelongsToCurrentThread());
  if (result != kIOReturnSuccess) {
    VLOG(1) << "Failed to read input report: " << base::StringPrintf("0x%08x",
                                                                     result);
    return;
  }

  scoped_refptr<net::IOBufferWithSize> buffer;
  if (connection->device_info().has_report_id) {
    // report_id is already contained in report_bytes
    buffer = new net::IOBufferWithSize(report_length);
    memcpy(buffer->data(), report_bytes, report_length);
  } else {
    buffer = new net::IOBufferWithSize(report_length + 1);
    buffer->data()[0] = 0;
    memcpy(buffer->data() + 1, report_bytes, report_length);
  }

  connection->task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&HidConnectionMac::ProcessInputReport, connection, buffer));
}

void HidConnectionMac::ProcessInputReport(
    scoped_refptr<net::IOBufferWithSize> buffer) {
  DCHECK(thread_checker().CalledOnValidThread());
  PendingHidReport report;
  report.buffer = buffer;
  report.size = buffer->size();
  pending_reports_.push(report);
  ProcessReadQueue();
}

void HidConnectionMac::ProcessReadQueue() {
  DCHECK(thread_checker().CalledOnValidThread());
  while (pending_reads_.size() && pending_reports_.size()) {
    PendingHidRead read = pending_reads_.front();
    PendingHidReport report = pending_reports_.front();

    pending_reports_.pop();
    if (CompleteRead(report.buffer, report.size, read.callback)) {
      pending_reads_.pop();
    }
  }
}

void HidConnectionMac::WriteReport(IOHIDReportType report_type,
                                   scoped_refptr<net::IOBuffer> buffer,
                                   size_t size,
                                   const WriteCallback& callback) {
  uint8_t* data = reinterpret_cast<uint8_t*>(buffer->data());
  DCHECK_GE(size, 1u);
  uint8_t report_id = data[0];
  if (report_id == 0) {
    // OS X only expects the first byte of the buffer to be the report ID if the
    // report ID is non-zero.
    ++data;
    --size;
  }

  // The IOHIDDevice object is shared with the UI thread and so this function
  // should probably be called there but it may block and the asynchronous
  // version is NOT IMPLEMENTED. I've examined the open source implementation
  // of this function and believe it to be thread-safe.
  IOReturn result =
      IOHIDDeviceSetReport(device_.get(), report_type, report_id, data, size);
  if (result == kIOReturnSuccess) {
    callback.Run(true);
  } else {
    VLOG(1) << "Failed to set report: " << base::StringPrintf("0x%08x", result);
    callback.Run(false);
  }
}

}  // namespace device
