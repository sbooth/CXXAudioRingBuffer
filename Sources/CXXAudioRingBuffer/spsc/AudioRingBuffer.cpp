//
// SPDX-FileCopyrightText: 2013 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXAudioRingBuffer
//

#include "spsc/AudioRingBuffer.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace {

/// Returns true if the ring buffer can store audio in the specified format.
bool isSupportedFormat(const AudioStreamBasicDescription &format) noexcept {
    if (format.mBytesPerFrame == 0 || format.mChannelsPerFrame == 0) {
        return false;
    }

    if (format.mFormatID != kAudioFormatLinearPCM ||
        (format.mFormatFlags & (kAudioFormatFlagIsFloat | kAudioFormatFlagIsSignedInteger)) == 0) {
        return false;
    }

    // Mono audio has the same memory layout whether or not it is flagged as non-interleaved
    return format.mChannelsPerFrame == 1 || (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
}

/// Returns the per-channel frame capacity to allocate for a supported format, or 0 if the capacity is not supported.
std::size_t perChannelFrameCapacity(const AudioStreamBasicDescription &format, std::size_t minFrameCapacity) noexcept {
    using spsc::AudioRingBuffer;
    if (minFrameCapacity < AudioRingBuffer::minCapacity || minFrameCapacity > AudioRingBuffer::maxCapacity) {
        return 0;
    }

    /// Values larger than this will overflow AudioBuffer.mDataByteSize
    const auto maxAudioBufferFrameCount = std::numeric_limits<UInt32>::max() / format.mBytesPerFrame;
    /// Values larger than this will exceed the maximum allocation size
    const auto maxAllocationFrameCount =
            ((std::numeric_limits<std::size_t>::max() / format.mChannelsPerFrame) - sizeof(void *)) /
            format.mBytesPerFrame;

    /// The maximum size per channel buffer in audio frames
    const auto maxChannelBufferFrameSize =
            std::min(static_cast<std::size_t>(maxAudioBufferFrameCount), maxAllocationFrameCount);

    // Round up to the next power of two; bounded by maxCapacity so std::bit_ceil is well-defined
    const auto channelBufferFrameSize = std::bit_ceil(minFrameCapacity);
    if (channelBufferFrameSize > maxChannelBufferFrameSize) {
        return 0;
    }
    return channelBufferFrameSize;
}

} /* namespace */

// MARK: Construction and Destruction

spsc::AudioRingBuffer::AudioRingBuffer(const AudioStreamBasicDescription &format, SizeType minFrameCapacity) {
    if (!isSupportedFormat(format)) [[unlikely]] {
        throw std::invalid_argument("unsupported audio format");
    }
    if (perChannelFrameCapacity(format, minFrameCapacity) == 0) [[unlikely]] {
        throw std::invalid_argument("capacity out of range");
    }
    if (!allocate(format, minFrameCapacity)) [[unlikely]] {
        throw std::bad_alloc();
    }
}

spsc::AudioRingBuffer::AudioRingBuffer(AudioRingBuffer &&other) noexcept
    : buffers_{std::exchange(other.buffers_, nullptr)}, capacity_{std::exchange(other.capacity_, 0)},
      capacityMask_{std::exchange(other.capacityMask_, 0)}, format_{std::exchange(other.format_, {})},
      writePosition_{other.writePosition_.exchange(0, std::memory_order_relaxed)},
      readPosition_{other.readPosition_.exchange(0, std::memory_order_relaxed)} {}

auto spsc::AudioRingBuffer::operator=(AudioRingBuffer &&other) noexcept -> AudioRingBuffer & {
    if (this != &other) [[likely]] {
        std::free(buffers_);
        buffers_ = std::exchange(other.buffers_, nullptr);

        capacity_ = std::exchange(other.capacity_, 0);
        capacityMask_ = std::exchange(other.capacityMask_, 0);

        writePosition_.store(other.writePosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
        readPosition_.store(other.readPosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);

        format_ = std::exchange(other.format_, {});
    }
    return *this;
}

spsc::AudioRingBuffer::~AudioRingBuffer() noexcept { std::free(buffers_); }

// MARK: Buffer Management

bool spsc::AudioRingBuffer::allocate(const AudioStreamBasicDescription &format, SizeType minFrameCapacity) noexcept {
    if (!isSupportedFormat(format)) [[unlikely]] {
        return false;
    }

    const auto channelBufferFrameSize = perChannelFrameCapacity(format, minFrameCapacity);
    if (channelBufferFrameSize == 0) [[unlikely]] {
        return false;
    }

    const auto channelBufferByteSize = channelBufferFrameSize * format.mBytesPerFrame;
    const auto allocationSize = (channelBufferByteSize + sizeof(void *)) * format.mChannelsPerFrame;

    auto allocation = std::malloc(allocationSize);
    if (allocation == nullptr) [[unlikely]] {
        return false;
    }

    std::free(buffers_);

    // buffers_ must point to the base allocation address so std::free() works correctly in deallocate()
    buffers_ = reinterpret_cast<void **>(allocation);

    // Advance past the void * array
    auto address = reinterpret_cast<std::uintptr_t>(allocation);
    address += format.mChannelsPerFrame * sizeof(void *);

    // Assign the channel buffers
    for (UInt32 i = 0; i < format.mChannelsPerFrame; ++i) {
        buffers_[i] = reinterpret_cast<void *>(address);
        address += channelBufferByteSize;
    }

    capacity_ = channelBufferFrameSize;
    capacityMask_ = channelBufferFrameSize - 1;

    writePosition_.store(0, std::memory_order_relaxed);
    readPosition_.store(0, std::memory_order_relaxed);

    format_ = format;

    return true;
}

void spsc::AudioRingBuffer::deallocate() noexcept {
    if (buffers_ != nullptr) [[likely]] {
        std::free(buffers_);
        buffers_ = nullptr;

        capacity_ = 0;
        capacityMask_ = 0;

        writePosition_.store(0, std::memory_order_relaxed);
        readPosition_.store(0, std::memory_order_relaxed);

        format_ = {};
    }
}
