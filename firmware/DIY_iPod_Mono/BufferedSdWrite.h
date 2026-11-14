#pragma once
// No Arduino dependencies: host-testable chunk accumulator for the transfer
// temporary file. ONLY the Sync task owns the File; no concurrent SD writes.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace BufferedSdWrite {
class Buffer {
 public:
  void reset(uint8_t* storage, size_t capacity) {
    storage_ = storage; capacity_ = capacity; used_ = 0;
  }
  size_t pending() const { return used_; }
  size_t capacity() const { return capacity_; }

  // writeFn(ptr, size) returns number of bytes actually written to the SD file.
  template<class WriteFn> bool flush(WriteFn writeFn) {
    if (!used_) return true;
    if (!writeAll(storage_, used_, writeFn)) return false;
    used_ = 0;
    return true;
  }
  template<class WriteFn> bool append(const uint8_t* data, size_t size, WriteFn writeFn) {
    if (!size) return true;
    if (!data) return false;
    if (!storage_ || capacity_ == 0) return writeAll(data, size, writeFn);
    while (size) {
      if (used_ == capacity_ && !flush(writeFn)) return false;
      // Avoid a duplicate copy for a large, already-aligned request.
      if (!used_ && size >= capacity_) return writeAll(data, size, writeFn);
      const size_t room = capacity_ - used_;
      const size_t count = size < room ? size : room;
      memcpy(storage_ + used_, data, count);
      used_ += count; data += count; size -= count;
    }
    return true;
  }
 private:
  template<class WriteFn> static bool writeAll(const uint8_t* data, size_t size, WriteFn writeFn) {
    while (size) {
      const size_t written = writeFn(data, size);
      if (!written || written > size) return false;
      data += written; size -= written;
    }
    return true;
  }
  uint8_t* storage_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
};
} // namespace BufferedSdWrite
