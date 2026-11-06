/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/devices/host_path_file.h>

#include <algorithm>
#include <cstring>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(
    host_path_mapped_reads, false, "Filesystem",
    "Serve read-only host-path files from a memory mapping instead of issuing "
    "a Win32/POSIX file read for every guest read. Existing open files keep "
    "their current path; restart is recommended when changing this setting.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::filesystem {

HostPathFile::HostPathFile(uint32_t file_access, HostPathEntry* entry,
                           std::unique_ptr<rex::filesystem::FileHandle> file_handle)
    : File(file_access, entry), file_handle_(std::move(file_handle)) {
  const bool readable =
      (file_access & (FileAccess::kGenericRead | FileAccess::kGenericAll |
                      FileAccess::kFileReadData)) != 0;
  const bool writable =
      (file_access & (FileAccess::kGenericWrite | FileAccess::kGenericAll |
                      FileAccess::kFileWriteData | FileAccess::kFileAppendData)) != 0;

  // Fable's game:/d: content mount is read-only by default. Mapping that
  // immutable content lets ordinary guest reads become memcpy from the OS page
  // cache while preserving the existing FileHandle fallback for empty files,
  // directories, mapping failures and anything writable.
  if (REXCVAR_GET(host_path_mapped_reads) && readable && !writable && entry &&
      entry->device()->is_read_only() && entry->size() != 0) {
    mapped_memory_ =
        entry->OpenMapped(rex::memory::MappedMemory::Mode::kRead, 0, 0);
  }
}

HostPathFile::~HostPathFile() = default;

void HostPathFile::Destroy() {
  delete this;
}

X_STATUS HostPathFile::ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                                size_t* out_bytes_read) {
  if (!(file_access_ & (FileAccess::kGenericRead | FileAccess::kFileReadData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (mapped_memory_) {
    if (byte_offset >= mapped_memory_->size()) {
      *out_bytes_read = 0;
      return X_STATUS_END_OF_FILE;
    }
    const size_t read_length =
        std::min(buffer.size(), mapped_memory_->size() - byte_offset);
    if (read_length != 0) {
      std::memcpy(buffer.data(), mapped_memory_->data() + byte_offset,
                  read_length);
    }
    *out_bytes_read = read_length;
    return X_STATUS_SUCCESS;
  }

  if (file_handle_->Read(byte_offset, buffer.data(), buffer.size(),
                         out_bytes_read)) {
    return X_STATUS_SUCCESS;
  }
  return X_STATUS_END_OF_FILE;
}

X_STATUS HostPathFile::WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                                 size_t* out_bytes_written) {
  if (!(file_access_ &
        (FileAccess::kGenericWrite | FileAccess::kFileWriteData | FileAccess::kFileAppendData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->Write(byte_offset, buffer.data(), buffer.size(), out_bytes_written)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

X_STATUS HostPathFile::SetLength(size_t length) {
  if (!(file_access_ & (FileAccess::kGenericWrite | FileAccess::kFileWriteData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->SetLength(length)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

}  // namespace rex::filesystem
