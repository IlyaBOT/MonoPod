#pragma once
// Presents FLAC to libFLAC with PICTURE payloads virtually removed.
// A PICTURE header is replaced with a valid empty PADDING header (4 bytes).
// This preserves the FLAC metadata chain (including its LAST flag), retains
// VORBIS_COMMENT and STREAMINFO, and does not modify the original SD file.
// Neither pictures nor any other large metadata payloads are allocated.
#include <AudioFileSource.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

class FlacPictureSkipSource final : public AudioFileSource {
public:
  static constexpr unsigned MAX_PICTURES = 32;
  static constexpr unsigned MAX_METADATA_BLOCKS = 256;
  explicit FlacPictureSkipSource(AudioFileSource* wrapped) : src(wrapped) {
    indexed = scan();
    if (src) src->seek(0, SEEK_SET);
  }
  bool valid() const { return indexed; }
  unsigned skippedPictures() const { return count; }
  uint32_t skippedBytes() const { return removedBytes; }
  bool isOpen() override { return indexed && src && src->isOpen(); }
  bool close() override { logicalPos = 0; return src && src->close(); }
  uint32_t getSize() override { return indexed ? virtualSize : 0; }
  uint32_t getPos() override { return logicalPos; }
  bool loop() override { return src && src->loop(); }
  uint32_t readNonBlock(void* dst, uint32_t amount) override { return read(dst, amount); }
  bool seek(int32_t offset, int dir) override {
    if (!indexed || !src) return false;
    int64_t origin = dir == SEEK_SET ? 0 : dir == SEEK_CUR ? (int64_t)logicalPos :
                     dir == SEEK_END ? (int64_t)virtualSize : -1;
    if (origin < 0) return false;
    int64_t target = origin + offset;
    if (target < 0 || target > virtualSize) return false;
    logicalPos = (uint32_t)target;
    return true;
  }
  uint32_t read(void* dst, uint32_t amount) override {
    if (!indexed || !src || !dst || !amount || logicalPos >= virtualSize) return 0;
    uint8_t* out = (uint8_t*)dst;
    amount = std::min(amount, virtualSize - logicalPos);
    uint32_t total = 0;
    while (amount) {
      unsigned pic = count;
      uint32_t nextHeader = virtualSize;
      uint32_t delta = 0;
      for (unsigned i = 0; i < count; i++) {
        const Picture& p = pictures[i];
        if (logicalPos >= p.virtualHeader && logicalPos < p.virtualHeader + 4) {
          pic = i;
          break;
        }
        if (logicalPos >= p.virtualHeader + 4) delta += p.length;
        else if (p.virtualHeader > logicalPos) { nextHeader = p.virtualHeader; break; }
      }
      if (pic != count) {
        const Picture& p = pictures[pic];
        const uint8_t paddingHeader[4] = { (uint8_t)((p.last ? 0x80 : 0) | 1), 0, 0, 0 };
        const uint32_t from = logicalPos - p.virtualHeader;
        const uint32_t n = std::min(amount, 4u - from);
        memcpy(out + total, paddingHeader + from, n);
        logicalPos += n; total += n; amount -= n;
        continue;
      }
      const uint32_t physicalPos = logicalPos + delta;
      const uint32_t n = std::min(amount, nextHeader - logicalPos);
      if (n == 0 || physicalPos > INT32_MAX) break;
      if (src->getPos() != physicalPos && !src->seek((int32_t)physicalPos, SEEK_SET)) break;
      uint32_t got = src->read(out + total, n);
      if (!got) break;
      logicalPos += got; total += got; amount -= got;
      if (got != n) break;
    }
    return total;
  }
private:
  struct Picture {
    uint32_t virtualHeader;
    uint32_t length;
    bool last;
  };
  bool scan() {
    if (!src || !src->isOpen()) return false;
    const uint32_t size = src->getSize();
    if (size < 42 || size > INT32_MAX || !src->seek(0, SEEK_SET)) return false;
    uint8_t h[4];
    if (src->read(h, 4) != 4 || memcmp(h, "fLaC", 4) != 0) return false;
    uint32_t pos = 4;
    bool streamInfo = false;
    for (unsigned block = 0; block < MAX_METADATA_BLOCKS; ++block) {
      if ((uint64_t)pos + 4 > size || !src->seek((int32_t)pos, SEEK_SET) || src->read(h, 4) != 4) return false;
      const bool last = (h[0] & 0x80) != 0;
      const uint8_t type = h[0] & 0x7f;
      const uint32_t n = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
      const uint64_t end = (uint64_t)pos + 4 + n;
      if (type == 127 || end > size) return false;
      if (block == 0) {
        if (type != 0 || n != 34) return false;
        streamInfo = true;
      } else if (type == 0) return false;
      if (type == 6) {
        if (count >= MAX_PICTURES) return false;
        pictures[count++] = {pos - removedBytes, n, last};
        removedBytes += n;
      }
      pos = (uint32_t)end;
      if (last) {
        virtualSize = size - removedBytes;
        return streamInfo;
      }
    }
    return false;
  }
  AudioFileSource* src = nullptr;
  Picture pictures[MAX_PICTURES]{};
  unsigned count = 0;
  uint32_t logicalPos = 0, virtualSize = 0, removedBytes = 0;
  bool indexed = false;
};
