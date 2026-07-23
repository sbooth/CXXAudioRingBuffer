//
// SPDX-FileCopyrightText: 2013 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXAudioRingBuffer
//

#include "spsc/AudioRingBuffer.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <utility>

// MARK: Construction and Destruction

spsc::AudioRingBuffer::AudioRingBuffer(const AudioStreamBasicDescription &format, SizeType minFrameCapacity) {
    if ((format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0 || format.mBytesPerFrame == 0 ||
        format.mChannelsPerFrame == 0) [[unlikely]] {
        throw std::invalid_argument("unsupported audio format");
    }
    if (minFrameCapacity < minCapacity || minFrameCapacity > maxCapacity) [[unlikely]] {
        throw std::invalid_argument("capacity out of range");
    }
    if (!allocate(format, minFrameCapacity)) [[unlikely]] {
        throw std::bad_alloc();
    }
}

spsc::AudioRingBuffer::AudioRingBuffer(AudioRingBuffer &&other) noexcept
    : buffers_{std::exchange(other.buffers_, nullptr)}, capacity_{std::exchange(other.capacity_, 0)},
      capacityMask_{std::exchange(other.capacityMask_, 0)},
      writePosition_{other.writePosition_.exchange(0, std::memory_order_relaxed)},
      readPosition_{other.readPosition_.exchange(0, std::memory_order_relaxed)},
      format_{std::exchange(other.format_, {})} {}

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
    if ((format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0 || format.mBytesPerFrame == 0 ||
        format.mChannelsPerFrame == 0) [[unlikely]] {
        return false;
    }
    if (minFrameCapacity < minCapacity || minFrameCapacity > maxCapacity) [[unlikely]] {
        return false;
    }

#if defined(__AVX512F__)
    constexpr std::size_t alignment = 64;
#elif defined(__AVX__) || defined(__AVX2__)
    constexpr std::size_t alignment = 32;
#elif defined(__SSE2__) || defined(__ARM_NEON) || defined(_M_X64) || defined(_M_ARM64)
    constexpr std::size_t alignment = 16;
#else
    constexpr std::size_t alignment = alignof(std::max_align_t);
#endif

    static_assert(std::has_single_bit(alignment), "alignment must be a power of two");

    /// Rounds `n` to the next higher multiple of `align`
    const auto alignUp = [](auto n, std::size_t align) noexcept {
        using T = decltype(n);
        static_assert(std::is_unsigned_v<T>, "n must be an unsigned integer");

        const auto mask = static_cast<T>(align) - 1;
        return (n + mask) & ~mask;
    };

    /// Values larger than this will overflow AudioBuffer.mDataByteSize
    const auto maxAudioBufferFrameCount = std::numeric_limits<UInt32>::max() / format.mBytesPerFrame;

    // Account for pointer array, initial offset padding, and worst-case per-channel alignment padding
    const auto perChannelOverhead = sizeof(void *) + alignment;
    const auto reserved = static_cast<std::size_t>(format.mChannelsPerFrame) * perChannelOverhead + alignment;

    /// Values larger than this will exceed the maximum allocation size
    const auto maxAllocationFrameCount =
            reserved < std::numeric_limits<std::size_t>::max()
                    ? ((std::numeric_limits<std::size_t>::max() - reserved) / format.mChannelsPerFrame) /
                              format.mBytesPerFrame
                    : 0;

    /// The maximum size per channel buffer in audio frames
    const auto maxChannelBufferFrameSize =
            std::min(static_cast<std::size_t>(maxAudioBufferFrameCount), maxAllocationFrameCount);

    // Round up to nearest power of two
    const auto channelBufferFrameSize = std::bit_ceil(minFrameCapacity);
    if (channelBufferFrameSize > maxChannelBufferFrameSize) [[unlikely]] {
        return false;
    }

    const auto channelBufferByteSize = channelBufferFrameSize * format.mBytesPerFrame;
    const auto alignedChannelByteSize = alignUp(channelBufferByteSize, alignment);
    const auto pointerArraySize = format.mChannelsPerFrame * sizeof(void *);
    const auto allocationSize =
            pointerArraySize + (alignment - 1) + (alignedChannelByteSize * format.mChannelsPerFrame);

    auto *allocation = std::malloc(allocationSize);
    if (allocation == nullptr) [[unlikely]] {
        return false;
    }

    std::free(buffers_);

    // buffers_ must point to the base allocation address so std::free() works correctly in deallocate()
    buffers_ = reinterpret_cast<void **>(allocation);

    // Advance past the void * array
    auto address = alignUp(reinterpret_cast<uintptr_t>(allocation) + pointerArraySize, alignment);

    // Assign the channel buffers
    for (UInt32 i = 0; i < format.mChannelsPerFrame; ++i) {
        buffers_[i] = reinterpret_cast<void *>(address);
        address += alignedChannelByteSize;
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
