#ifndef DFTRACER_COMPRESSION_ZLIB_COMPRESSION_H
#define DFTRACER_COMPRESSION_ZLIB_COMPRESSION_H

#include <dftracer/core/common/logging.h>
//
#include <stddef.h>  // Include this first to ensure size_t is defined
//
#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace dftracer {
class ZlibCompression {
 public:
  ZlibCompression() : chunk_size_(0) {}

  int initialize(size_t chunk_size) {
    chunk_size_ = chunk_size + 16 * 1024;
    strm_.zalloc = Z_NULL;
    strm_.zfree = Z_NULL;
    strm_.opaque = Z_NULL;
    if (deflateInit2(&strm_, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
      DFTRACER_LOG_ERROR("Failed to initialize zlib for gzip compression");
      return -1;
    }
    return 0;
  }

  ~ZlibCompression() {}

  // Compresses data from buffer starting at position, in place.
  // Returns the number of bytes written to buffer (compressed size).
  size_t compress(char* buffer, size_t data_size) {
    std::vector<char> out_buffer(chunk_size_);
    strm_.avail_in = static_cast<uInt>(data_size);
    strm_.next_in = reinterpret_cast<Bytef*>(buffer);
    strm_.avail_out = static_cast<uInt>(chunk_size_);
    strm_.next_out = reinterpret_cast<Bytef*>(out_buffer.data());

    int ret = deflate(&strm_, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
      DFTRACER_LOG_ERROR("Compression failed");
      return 0;
    }

    size_t compressed_size = chunk_size_ - strm_.avail_out;
    std::memcpy(buffer, out_buffer.data(), compressed_size);

    deflateReset(&strm_);
    return compressed_size;
  }

  int finalize() {
    deflateEnd(&strm_);
    return 0;
  }

 private:
  size_t chunk_size_;
  z_stream strm_;
};
}  // namespace dftracer

#endif  // DFTRACER_COMPRESSION_ZLIB_COMPRESSION_H
