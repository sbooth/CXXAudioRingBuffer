//
// SPDX-FileCopyrightText: 2013 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXAudioRingBuffer
//

#pragma once

#include <CoreAudioTypes/CoreAudioTypes.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

namespace spsc {

/// A lock-free SPSC ring buffer supporting non-interleaved audio.
///
/// This class is thread safe when used with a single producer and a single consumer.
class AudioRingBuffer final {
  public:
    /// Unsigned integer type.
    using SizeType = std::size_t;
    /// Atomic unsigned integer type.
    using AtomicSizeType = std::atomic<SizeType>;

    /// The minimum supported buffer capacity in audio frames.
    static constexpr SizeType minCapacity = SizeType{2};
    /// The maximum supported buffer capacity in audio frames.
    static constexpr SizeType maxCapacity = SizeType{1} << (std::numeric_limits<SizeType>::digits - 1);

    // MARK: Construction and Destruction

    /// Creates an empty ring buffer.
    /// @note ``allocate`` must be called before the object may be used.
    AudioRingBuffer() noexcept = default;

    /// Creates a ring buffer with the specified format and minimum audio frame capacity.
    ///
    /// The actual buffer capacity will be the smallest integral power of two that is not less than the specified
    /// minimum capacity.
    /// @note Only non-interleaved formats are supported.
    /// @param format The format of the audio that will be written to and read from the buffer.
    /// @param minFrameCapacity The desired minimum capacity in audio frames.
    /// @throw std::bad_alloc if memory could not be allocated or std::invalid_argument if the buffer capacity is not
    /// supported.
    AudioRingBuffer(const AudioStreamBasicDescription &format, SizeType minFrameCapacity);

    // This class is non-copyable
    AudioRingBuffer(const AudioRingBuffer &) = delete;

    /// Creates a ring buffer by moving the contents of another ring buffer.
    /// @note This method is not thread safe for the ring buffer being moved.
    /// @param other The ring buffer to move.
    AudioRingBuffer(AudioRingBuffer &&other) noexcept;

    // This class is non-assignable
    AudioRingBuffer &operator=(const AudioRingBuffer &) = delete;

    /// Moves the contents of another ring buffer into this ring buffer.
    /// @note This method is not thread safe.
    /// @param other The ring buffer to move.
    AudioRingBuffer &operator=(AudioRingBuffer &&other) noexcept;

    /// Destroys the ring buffer and releases all associated resources.
    ~AudioRingBuffer() noexcept;

    // MARK: Buffer Management

    /// Allocates space for audio data of the specified format.
    ///
    /// The actual buffer capacity will be the smallest integral power of two that is not less than the specified
    /// minimum capacity.
    /// @note Only non-interleaved formats are supported.
    /// @note This method is not thread safe.
    /// @param format The format of the audio that will be written to and read from this buffer.
    /// @param minFrameCapacity The desired minimum capacity in audio frames.
    /// @return true on success, false if memory could not be allocated, the audio format is not supported, or the
    /// buffer capacity is not supported.
    [[nodiscard]] bool allocate(const AudioStreamBasicDescription &format, SizeType minFrameCapacity) noexcept
            [[clang::allocating]];

    /// Frees any space allocated for audio data.
    /// @note This method is not thread safe.
    void deallocate() noexcept [[clang::allocating]];

    /// Returns true if the buffer has allocated space for audio data.
    [[nodiscard]] explicit operator bool() const noexcept [[clang::nonblocking]];

    // MARK: Buffer Information

    /// Returns the format of the audio stored in the buffer.
    /// @note This method is safe to call from both producer and consumer.
    /// @return The audio format of the buffer.
    [[nodiscard]] const AudioStreamBasicDescription &format() const noexcept [[clang::nonblocking]];

    /// Returns the capacity of the buffer.
    /// @note This method is safe to call from both producer and consumer.
    /// @return The buffer capacity in audio frames.
    [[nodiscard]] SizeType capacity() const noexcept [[clang::nonblocking]];

    /// Returns the current free-running write position in the buffer.
    ///
    /// This value is a monotonically increasing frame counter not wrapped to the buffer's capacity.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return The current free-running write position in audio frames.
    [[nodiscard]] SizeType writePosition() const noexcept [[clang::nonblocking]];

    /// Returns the current free-running read position in the buffer.
    ///
    /// This value is a monotonically increasing frame counter not wrapped to the buffer's capacity.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return The current free-running read position in audio frames.
    [[nodiscard]] SizeType readPosition() const noexcept [[clang::nonblocking]];

    // MARK: Buffer Usage

    /// Returns true if the buffer is full.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return true if the buffer is full.
    [[nodiscard]] bool isFull() const noexcept [[clang::nonblocking]];

    /// Returns true if the buffer is empty.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return true if the buffer contains no data.
    [[nodiscard]] bool isEmpty() const noexcept [[clang::nonblocking]];

    /// Returns the frame count of free space in the buffer.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return The number of audio frames of free space available for writing.
    [[nodiscard]] SizeType availableToWrite() const noexcept [[clang::nonblocking]];

    /// Returns the frame count of audio in the buffer.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return The number of audio frames available for reading.
    [[nodiscard]] SizeType availableToRead() const noexcept [[clang::nonblocking]];

    // MARK: Writing and Reading Audio

    /// Writes audio and advances the write position.
    /// @note This method is only safe to call from the producer.
    /// @param bufferList An audio buffer list containing the data to copy.
    /// @param frameCount The desired number of audio frames to write.
    /// @return The number of audio frames actually written.
    [[nodiscard]] SizeType write(const AudioBufferList &bufferList, SizeType frameCount) noexcept
            [[clang::nonblocking]];

    /// Reads audio and advances the read position.
    ///
    /// If fewer than the requested number of frames are available the remainder of the audio buffer list will be set to
    /// zero.
    /// @note This method is only safe to call from the consumer.
    /// @param bufferList An audio buffer list to receive the data.
    /// @param frameCount The desired number of audio frames to read.
    /// @return The number of audio frames actually read.
    [[nodiscard]] SizeType read(AudioBufferList &bufferList, SizeType frameCount) noexcept [[clang::nonblocking]];

    // MARK: Discarding Audio

    /// Discards audio and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param frameCount The desired number of audio frames to discard.
    /// @return The number of audio frames actually discarded.
    SizeType discard(SizeType frameCount) noexcept [[clang::nonblocking]];

    /// Discards all audio from the buffer and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @return The number of audio frames discarded.
    SizeType discardAll() noexcept [[clang::nonblocking]];

  private:
    /// The memory buffers holding the data, consisting of channel pointers and buffers allocated in one chunk.
    void *_Nonnull *_Nullable buffers_{nullptr};

    /// The per-channel capacity of ``buffers_`` in audio frames.
    SizeType capacity_{0};
    /// The per-channel capacity of ``buffers_`` in audio frames minus one.
    SizeType capacityMask_{0};

    /// The free-running write location.
    alignas(std::hardware_destructive_interference_size) AtomicSizeType writePosition_{0};
    /// The free-running read location.
    alignas(std::hardware_destructive_interference_size) AtomicSizeType readPosition_{0};

    static_assert(AtomicSizeType::is_always_lock_free, "Lock-free AtomicSizeType required");

    /// The format of the audio this buffer contains.
    AudioStreamBasicDescription format_{};
};

// MARK: - Implementation -

// MARK: Buffer Management

inline AudioRingBuffer::operator bool() const noexcept { return buffers_ != nullptr; }

// MARK: Buffer Information

inline const AudioStreamBasicDescription &AudioRingBuffer::format() const noexcept { return format_; }

inline auto AudioRingBuffer::capacity() const noexcept -> SizeType { return capacity_; }

inline auto AudioRingBuffer::writePosition() const noexcept -> SizeType {
    return writePosition_.load(std::memory_order_relaxed);
}

inline auto AudioRingBuffer::readPosition() const noexcept -> SizeType {
    return readPosition_.load(std::memory_order_relaxed);
}

// MARK: Buffer Usage

inline bool AudioRingBuffer::isFull() const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return (writePos - readPos) == capacity_;
}

inline bool AudioRingBuffer::isEmpty() const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    return writePos == readPos;
}

inline auto AudioRingBuffer::availableToWrite() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return capacity_ - (writePos - readPos);
}

inline auto AudioRingBuffer::availableToRead() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    return writePos - readPos;
}

// MARK: Writing and Reading Audio

inline auto AudioRingBuffer::write(const AudioBufferList &bufferList, SizeType frameCount) noexcept -> SizeType {
    if (bufferList.mNumberBuffers != format_.mChannelsPerFrame || frameCount == 0 || capacity_ == 0) [[unlikely]] {
        assert(bufferList.mNumberBuffers == format_.mChannelsPerFrame);
        return 0;
    }

    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    const auto availableToRead = writePos - readPos;

    if (availableToRead > capacity_) [[unlikely]] {
        assert(false && "Buffer invariant violated: (writePosition_ - readPosition_) exceeds capacity_");
        return 0;
    }

    const auto availableToWrite = capacity_ - availableToRead;
    const auto framesToWrite = std::min(availableToWrite, frameCount);

    if (framesToWrite == 0) [[unlikely]] {
        return 0;
    }

    const auto bytesToWrite = framesToWrite * format_.mBytesPerFrame;
    const auto channelCount = bufferList.mNumberBuffers;

    const auto writeIndex = writePos & capacityMask_;
    const auto framesToEnd = capacity_ - writeIndex;

    if (framesToWrite <= framesToEnd) [[likely]] {
        const auto dstOffset = writeIndex * format_.mBytesPerFrame;
        for (UInt32 i = 0; i < channelCount; ++i) {
            assert(bufferList.mBuffers[i].mData != nullptr);
            assert(bytesToWrite <= bufferList.mBuffers[i].mDataByteSize);
            std::memcpy(static_cast<unsigned char *>(buffers_[i]) + dstOffset, bufferList.mBuffers[i].mData,
                        bytesToWrite);
        }
    } else [[unlikely]] {
        const auto bytesToEnd = framesToEnd * format_.mBytesPerFrame;
        const auto bytesFromStart = bytesToWrite - bytesToEnd;

        for (UInt32 i = 0; i < channelCount; ++i) {
            assert(bufferList.mBuffers[i].mData != nullptr);
            assert(bytesToWrite <= bufferList.mBuffers[i].mDataByteSize);
            auto *dst = static_cast<unsigned char *>(buffers_[i]);
            const auto *src = static_cast<const unsigned char *>(bufferList.mBuffers[i].mData);
            std::memcpy(dst + (writeIndex * format_.mBytesPerFrame), src, bytesToEnd);
            std::memcpy(dst, src + bytesToEnd, bytesFromStart);
        }
    }

    writePosition_.store(writePos + framesToWrite, std::memory_order_release);
    return framesToWrite;
}

inline auto AudioRingBuffer::read(AudioBufferList &bufferList, SizeType frameCount) noexcept -> SizeType {
    if (bufferList.mNumberBuffers != format_.mChannelsPerFrame || frameCount == 0 || capacity_ == 0) [[unlikely]] {
        assert(bufferList.mNumberBuffers == format_.mChannelsPerFrame);
        return 0;
    }

    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    auto availableToRead = writePos - readPos;

    if (availableToRead > capacity_) [[unlikely]] {
        assert(false && "Buffer invariant violated: (writePosition_ - readPosition_) exceeds capacity_");
        availableToRead = 0;
    }

    const auto framesToRead = std::min(availableToRead, frameCount);
    const auto bytesToRead = framesToRead * format_.mBytesPerFrame;
    const auto channelCount = bufferList.mNumberBuffers;

    if (framesToRead > 0) [[likely]] {
        const auto readIndex = readPos & capacityMask_;
        const auto framesToEnd = capacity_ - readIndex;

        if (framesToRead <= framesToEnd) [[likely]] {
            const auto srcOffset = readIndex * format_.mBytesPerFrame;
            for (UInt32 i = 0; i < channelCount; ++i) {
                assert(bufferList.mBuffers[i].mData != nullptr);
                assert(bytesToRead <= bufferList.mBuffers[i].mDataByteSize);
                std::memcpy(bufferList.mBuffers[i].mData, static_cast<const unsigned char *>(buffers_[i]) + srcOffset,
                            bytesToRead);
            }
        } else [[unlikely]] {
            const auto bytesToEnd = framesToEnd * format_.mBytesPerFrame;
            const auto bytesFromStart = bytesToRead - bytesToEnd;

            for (UInt32 i = 0; i < channelCount; ++i) {
                assert(bufferList.mBuffers[i].mData != nullptr);
                assert(bytesToRead <= bufferList.mBuffers[i].mDataByteSize);
                auto *dst = static_cast<unsigned char *>(bufferList.mBuffers[i].mData);
                const auto *src = static_cast<const unsigned char *>(buffers_[i]);
                std::memcpy(dst, src + (readIndex * format_.mBytesPerFrame), bytesToEnd);
                std::memcpy(dst + bytesToEnd, src, bytesFromStart);
            }
        }

        readPosition_.store(readPos + framesToRead, std::memory_order_release);
    }

    // Fill remainder with silence if fewer than requested frames were read (underrun or buffer empty)
    if (framesToRead < frameCount) [[unlikely]] {
        const auto byteOffset = bytesToRead;
        const auto zeroByteCount = (frameCount - framesToRead) * format_.mBytesPerFrame;

        for (UInt32 i = 0; i < channelCount; ++i) {
            assert(bufferList.mBuffers[i].mData != nullptr);
            assert(byteOffset + zeroByteCount <= bufferList.mBuffers[i].mDataByteSize);
            auto *dst = static_cast<unsigned char *>(bufferList.mBuffers[i].mData);
            std::memset(dst + byteOffset, 0, zeroByteCount);
        }
    }

    return framesToRead;
}

// MARK: Discarding Audio

inline auto AudioRingBuffer::discard(SizeType frameCount) noexcept -> SizeType {
    if (frameCount == 0 || capacity_ == 0) [[unlikely]] {
        return 0;
    }

    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto availableToRead = writePos - readPos;

    if (availableToRead == 0) {
        return 0;
    }

    if (availableToRead > capacity_) [[unlikely]] {
        assert(false && "Buffer invariant violated: (writePosition_ - readPosition_) exceeds capacity_");
        return 0;
    }

    const auto framesToDiscard = std::min(availableToRead, frameCount);
    readPosition_.store(readPos + framesToDiscard, std::memory_order_release);
    return framesToDiscard;
}

inline auto AudioRingBuffer::discardAll() noexcept -> SizeType { return discard(std::numeric_limits<SizeType>::max()); }

} /* namespace spsc */
