// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains an implementation of VaapiWrapper, used by
// VaapiVideoDecodeAccelerator and VaapiH264Decoder for decode,
// and VaapiVideoEncodeAccelerator for encode, to interface
// with libva (VA-API library for hardware video codec).

#ifndef CONTENT_COMMON_GPU_MEDIA_VAAPI_WRAPPER_H_
#define CONTENT_COMMON_GPU_MEDIA_VAAPI_WRAPPER_H_

#include <set>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "content/common/content_export.h"
#include "content/common/gpu/media/va_surface.h"
#include "media/base/video_decoder_config.h"
#include "media/base/video_frame.h"
#include "third_party/libva/va/va.h"
#include "third_party/libva/va/va_vpp.h"
#include "ui/gfx/size.h"
#if defined(USE_X11)
#include "third_party/libva/va/va_x11.h"
#endif  // USE_X11

namespace content {

// This class handles VA-API calls and ensures proper locking of VA-API calls
// to libva, the userspace shim to the HW codec driver. libva is not
// thread-safe, so we have to perform locking ourselves. This class is fully
// synchronous and its methods can be called from any thread and may wait on
// the va_lock_ while other, concurrent calls run.
//
// This class is responsible for managing VAAPI connection, contexts and state.
// It is also responsible for managing and freeing VABuffers (not VASurfaces),
// which are used to queue parameters and slice data to the HW codec,
// as well as underlying memory for VASurfaces themselves.
class CONTENT_EXPORT VaapiWrapper {
 public:
  enum CodecMode {
    kDecode,
    kEncode,
  };

  // |report_error_to_uma_cb| will be called independently from reporting
  // errors to clients via method return values.
  static scoped_ptr<VaapiWrapper> Create(
      CodecMode mode,
      media::VideoCodecProfile profile,
      const base::Closure& report_error_to_uma_cb);

  // Return the supported encode profiles.
  static std::vector<media::VideoCodecProfile> GetSupportedEncodeProfiles(
      const base::Closure& report_error_to_uma_cb);

  ~VaapiWrapper();

  // Create |num_surfaces| backing surfaces in driver for VASurfaces, each
  // of size |size|. Returns true when successful, with the created IDs in
  // |va_surfaces| to be managed and later wrapped in VASurfaces.
  // The client must DestroySurfaces() each time before calling this method
  // again to free the allocated surfaces first, but is not required to do so
  // at destruction time, as this will be done automatically from
  // the destructor.
  bool CreateSurfaces(const gfx::Size& size,
                      size_t num_surfaces,
                      std::vector<VASurfaceID>* va_surfaces);

  // Free all memory allocated in CreateSurfaces.
  void DestroySurfaces();

  // Create a VASurface of |va_format|, |size| and using |va_attribs|
  // attributes. The ownership of the surface is transferred to the
  // caller. It differs from surfaces created using CreateSurfaces(),
  // where VaapiWrapper is the owner of the surfaces.
  scoped_refptr<VASurface> CreateUnownedSurface(
      unsigned int va_format,
      const gfx::Size& size,
      const std::vector<VASurfaceAttrib>& va_attribs);

  // Submit parameters or slice data of |va_buffer_type|, copying them from
  // |buffer| of size |size|, into HW codec. The data in |buffer| is no
  // longer needed and can be freed after this method returns.
  // Data submitted via this method awaits in the HW codec until
  // ExecuteAndDestroyPendingBuffers() is called to execute or
  // DestroyPendingBuffers() is used to cancel a pending job.
  bool SubmitBuffer(VABufferType va_buffer_type, size_t size, void* buffer);

  // Submit a VAEncMiscParameterBuffer of given |misc_param_type|, copying its
  // data from |buffer| of size |size|, into HW codec. The data in |buffer| is
  // no longer needed and can be freed after this method returns.
  // Data submitted via this method awaits in the HW codec until
  // ExecuteAndDestroyPendingBuffers() is called to execute or
  // DestroyPendingBuffers() is used to cancel a pending job.
  bool SubmitVAEncMiscParamBuffer(VAEncMiscParameterType misc_param_type,
                                  size_t size,
                                  void* buffer);

  // Cancel and destroy all buffers queued to the HW codec via SubmitBuffer().
  // Useful when a pending job is to be cancelled (on reset or error).
  void DestroyPendingBuffers();

  // Execute job in hardware on target |va_surface_id| and destroy pending
  // buffers. Return false if Execute() fails.
  bool ExecuteAndDestroyPendingBuffers(VASurfaceID va_surface_id);

#if defined(USE_X11)
  // Put data from |va_surface_id| into |x_pixmap| of size
  // |dest_size|, converting/scaling to it.
  bool PutSurfaceIntoPixmap(VASurfaceID va_surface_id,
                            Pixmap x_pixmap,
                            gfx::Size dest_size);
#endif  // USE_X11

  // Returns true if the VAAPI version is less than the specified version.
  bool VAAPIVersionLessThan(int major, int minor);

  // Get a VAImage from a VASurface and map it into memory. The VAImage should
  // be released using the ReturnVaImage function. Returns true when successful.
  // This is intended for testing only.
  bool GetVaImageForTesting(VASurfaceID va_surface_id,
                            VAImage* image,
                            void** mem);

  // Release the VAImage (and the associated memory mapping) obtained from
  // GetVaImage(). This is intended for testing only.
  void ReturnVaImageForTesting(VAImage* image);

  // Upload contents of |frame| into |va_surface_id| for encode.
  bool UploadVideoFrameToSurface(const scoped_refptr<media::VideoFrame>& frame,
                                 VASurfaceID va_surface_id);

  // Create a buffer of |size| bytes to be used as encode output.
  bool CreateCodedBuffer(size_t size, VABufferID* buffer_id);

  // Download the contents of the buffer with given |buffer_id| into a buffer of
  // size |target_size|, pointed to by |target_ptr|. The number of bytes
  // downloaded will be returned in |coded_data_size|. |sync_surface_id| will
  // be used as a sync point, i.e. it will have to become idle before starting
  // the download. |sync_surface_id| should be the source surface passed
  // to the encode job.
  bool DownloadAndDestroyCodedBuffer(VABufferID buffer_id,
                                     VASurfaceID sync_surface_id,
                                     uint8* target_ptr,
                                     size_t target_size,
                                     size_t* coded_data_size);

  // Destroy all previously-allocated (and not yet destroyed) coded buffers.
  void DestroyCodedBuffers();

  // Blits a VASurface |va_surface_id_src| into another VASurface
  // |va_surface_id_dest| applying pixel format conversion and scaling
  // if needed.
  bool BlitSurface(VASurfaceID va_surface_id_src,
                   const gfx::Size& src_size,
                   VASurfaceID va_surface_id_dest,
                   const gfx::Size& dest_size);

 private:
  VaapiWrapper();

  bool Initialize(CodecMode mode,
                  media::VideoCodecProfile profile,
                  const base::Closure& report_error__to_uma_cb);
  void Deinitialize();
  bool VaInitialize(const base::Closure& report_error_to_uma_cb);
  bool GetSupportedVaProfiles(std::vector<VAProfile>* profiles);
  bool IsEntrypointSupported(VAProfile va_profile, VAEntrypoint entrypoint);
  bool AreAttribsSupported(VAProfile va_profile,
                           VAEntrypoint entrypoint,
                           const std::vector<VAConfigAttrib>& required_attribs);

  // Destroys a |va_surface| created using CreateUnownedSurface.
  void DestroyUnownedSurface(VASurfaceID va_surface_id);

  // Initialize the video post processing context with the |size| of
  // the input pictures to be processed.
  bool InitializeVpp_Locked();

  // Deinitialize the video post processing context.
  void DeinitializeVpp();

  // Execute pending job in hardware and destroy pending buffers. Return false
  // if vaapi driver refuses to accept parameter or slice buffers submitted
  // by client, or if execution fails in hardware.
  bool Execute(VASurfaceID va_surface_id);

  // Attempt to set render mode to "render to texture.". Failure is non-fatal.
  void TryToSetVADisplayAttributeToLocalGPU();

  // Lazily initialize static data after sandbox is enabled.  Return false on
  // init failure.
  static bool PostSandboxInitialization();

  // Libva is not thread safe, so we have to do locking for it ourselves.
  // This lock is to be taken for the duration of all VA-API calls and for
  // the entire job submission sequence in ExecuteAndDestroyPendingBuffers().
  base::Lock va_lock_;

  // Allocated ids for VASurfaces.
  std::vector<VASurfaceID> va_surface_ids_;

  // The VAAPI version.
  int major_version_, minor_version_;

  // VA handles.
  // All valid after successful Initialize() and until Deinitialize().
  VADisplay va_display_;
  VAConfigID va_config_id_;
  // Created for the current set of va_surface_ids_ in CreateSurfaces() and
  // valid until DestroySurfaces().
  VAContextID va_context_id_;
  // True if vaInitialize has been called successfully.
  bool va_initialized_;

  // Data queued up for HW codec, to be committed on next execution.
  std::vector<VABufferID> pending_slice_bufs_;
  std::vector<VABufferID> pending_va_bufs_;

  // Bitstream buffers for encode.
  std::set<VABufferID> coded_buffers_;

  // Called to report codec errors to UMA. Errors to clients are reported via
  // return values from public methods.
  base::Closure report_error_to_uma_cb_;

  // VPP (Video Post Processing) context, this is used to convert
  // pictures used by the decoder to RGBA pictures usable by GL or the
  // display hardware.
  VAConfigID va_vpp_config_id_;
  VAContextID va_vpp_context_id_;
  VABufferID va_vpp_buffer_id_;

  DISALLOW_COPY_AND_ASSIGN(VaapiWrapper);
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_MEDIA_VAAPI_WRAPPER_H_
