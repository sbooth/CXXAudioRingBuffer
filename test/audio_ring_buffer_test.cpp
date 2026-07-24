//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXAudioRingBuffer
//

#include "spsc/AudioRingBuffer.hpp"

#include <atomic>
#include <numeric>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace spsc {
namespace test {

// ============================================================================
// Helper Utilities for AudioBufferList creation and teardown
// ============================================================================

struct AudioBufferListStorage {
    std::vector<uint8_t> bufferData;
    std::vector<uint8_t> listStorage;
    AudioBufferList *bufferList{nullptr};

    AudioBufferListStorage(UInt32 numChannels, UInt32 frameCapacity, UInt32 bytesPerFrame) {
        const auto channelByteSize = frameCapacity * bytesPerFrame;
        bufferData.resize(numChannels * channelByteSize, 0);

        auto ablSize = sizeof(AudioBufferList) + (numChannels > 1 ? (numChannels - 1) * sizeof(AudioBuffer) : 0);
        listStorage.resize(ablSize, 0);
        bufferList = reinterpret_cast<AudioBufferList *>(listStorage.data());
        bufferList->mNumberBuffers = numChannels;

        for (UInt32 i = 0; i < numChannels; ++i) {
            bufferList->mBuffers[i].mNumberChannels = 1; // Non-interleaved
            bufferList->mBuffers[i].mDataByteSize = static_cast<UInt32>(channelByteSize);
            bufferList->mBuffers[i].mData = bufferData.data() + (i * channelByteSize);
        }
    }

    AudioBufferList &get() { return *bufferList; }
};

static AudioStreamBasicDescription makeNonInterleavedFloatFormat(UInt32 channels, Float64 sampleRate = 44100.0) {
    AudioStreamBasicDescription format{};
    format.mSampleRate = sampleRate;
    format.mFormatID = 0x6C70636D;               // kAudioFormatLinearPCM
    format.mFormatFlags = (1U << 0) | (1U << 5); // Float, Non-Interleaved
    format.mBytesPerPacket = sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float);
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 32;
    return format;
}

// ============================================================================
// Test Fixture
// ============================================================================

class AudioRingBufferTest : public ::testing::Test {
  protected:
    static constexpr UInt32 kChannels = 2;
    static constexpr AudioRingBuffer::SizeType kMinCapacity = 512;
    AudioStreamBasicDescription format_ = makeNonInterleavedFloatFormat(kChannels);
};

// ============================================================================
// Lifecycle & Initialization Tests
// ============================================================================

TEST_F(AudioRingBufferTest, DefaultConstructor) {
    AudioRingBuffer ringBuffer;
    EXPECT_FALSE(static_cast<bool>(ringBuffer));
    EXPECT_EQ(ringBuffer.capacity(), 0);
    EXPECT_EQ(ringBuffer.writePosition(), 0);
    EXPECT_EQ(ringBuffer.readPosition(), 0);
    EXPECT_TRUE(ringBuffer.isEmpty());
}

TEST_F(AudioRingBufferTest, AllocateAndPowerOfTwoCapacity) {
    AudioRingBuffer ringBuffer;
    // Requesting 500 should round up to 512 (power of two)
    EXPECT_TRUE(ringBuffer.allocate(format_, 500));
    EXPECT_TRUE(static_cast<bool>(ringBuffer));
    EXPECT_GE(ringBuffer.capacity(), 500);
    EXPECT_EQ(ringBuffer.capacity(), 512);
    EXPECT_EQ(ringBuffer.availableToWrite(), 512);
    EXPECT_EQ(ringBuffer.availableToRead(), 0);
    EXPECT_TRUE(ringBuffer.isEmpty());
    EXPECT_FALSE(ringBuffer.isFull());
}

TEST_F(AudioRingBufferTest, MoveConstruction) {
    AudioRingBuffer src(format_, kMinCapacity);
    AudioBufferListStorage srcABL(kChannels, 100, sizeof(float));

    // Populate dummy values
    for (std::size_t i = 0; i < 100; ++i) {
        reinterpret_cast<float *>(srcABL.bufferList->mBuffers[0].mData)[i] = static_cast<float>(i);
    }
    EXPECT_EQ(src.write(srcABL.get(), 100), 100);

    AudioRingBuffer dst(std::move(src));

    EXPECT_TRUE(static_cast<bool>(dst));
    EXPECT_FALSE(static_cast<bool>(src)); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(dst.availableToRead(), 100);
    EXPECT_EQ(dst.capacity(), kMinCapacity);
}

TEST_F(AudioRingBufferTest, MoveAssignment) {
    AudioRingBuffer src(format_, kMinCapacity);
    AudioRingBuffer dst;

    dst = std::move(src);

    EXPECT_TRUE(static_cast<bool>(dst));
    EXPECT_FALSE(static_cast<bool>(src)); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(dst.capacity(), kMinCapacity);
}

// ============================================================================
// Single-Threaded Read/Write Functionality
// ============================================================================

TEST_F(AudioRingBufferTest, BasicWriteAndRead) {
    AudioRingBuffer ringBuffer(format_, 1024);
    constexpr AudioRingBuffer::SizeType frameCount = 256;

    AudioBufferListStorage writeABL(kChannels, frameCount, sizeof(float));
    AudioBufferListStorage readABL(kChannels, frameCount, sizeof(float));

    // Fill source with recognizable audio samples
    auto *ch0Write = reinterpret_cast<float *>(writeABL.bufferList->mBuffers[0].mData);
    auto *ch1Write = reinterpret_cast<float *>(writeABL.bufferList->mBuffers[1].mData);
    for (std::size_t i = 0; i < frameCount; ++i) {
        ch0Write[i] = static_cast<float>(i) * 0.1f;
        ch1Write[i] = static_cast<float>(i) * -0.1f;
    }

    EXPECT_EQ(ringBuffer.write(writeABL.get(), frameCount), frameCount);
    EXPECT_EQ(ringBuffer.availableToRead(), frameCount);
    EXPECT_EQ(ringBuffer.availableToWrite(), 1024 - frameCount);

    EXPECT_EQ(ringBuffer.read(readABL.get(), frameCount), frameCount);
    EXPECT_EQ(ringBuffer.availableToRead(), 0);

    // Verify written data matches read data
    auto *ch0Read = reinterpret_cast<float *>(readABL.bufferList->mBuffers[0].mData);
    auto *ch1Read = reinterpret_cast<float *>(readABL.bufferList->mBuffers[1].mData);
    for (std::size_t i = 0; i < frameCount; ++i) {
        EXPECT_FLOAT_EQ(ch0Read[i], ch0Write[i]);
        EXPECT_FLOAT_EQ(ch1Read[i], ch1Write[i]);
    }
}

TEST_F(AudioRingBufferTest, RingBufferWrapAround) {
    constexpr AudioRingBuffer::SizeType cap = 256;
    AudioRingBuffer ringBuffer(format_, cap);

    AudioBufferListStorage writeABL(kChannels, 200, sizeof(float));
    AudioBufferListStorage readABL(kChannels, 200, sizeof(float));

    // Write 200 frames, read 200 frames -> writePos=200, readPos=200
    EXPECT_EQ(ringBuffer.write(writeABL.get(), 200), 200);
    EXPECT_EQ(ringBuffer.read(readABL.get(), 200), 200);

    // Write 100 frames -> this will wrap around the boundary of 256
    for (std::size_t i = 0; i < 100; ++i) {
        reinterpret_cast<float *>(writeABL.bufferList->mBuffers[0].mData)[i] = static_cast<float>(i + 1);
        reinterpret_cast<float *>(writeABL.bufferList->mBuffers[1].mData)[i] = static_cast<float>(i + 1);
    }

    EXPECT_EQ(ringBuffer.write(writeABL.get(), 100), 100);
    EXPECT_EQ(ringBuffer.writePosition(), 300);

    // Read wrapped frames
    std::fill_n(reinterpret_cast<float *>(readABL.bufferList->mBuffers[0].mData), 100, 0.0f);
    EXPECT_EQ(ringBuffer.read(readABL.get(), 100), 100);

    auto *ch0 = reinterpret_cast<float *>(readABL.bufferList->mBuffers[0].mData);
    for (std::size_t i = 0; i < 100; ++i) {
        EXPECT_FLOAT_EQ(ch0[i], static_cast<float>(i + 1));
    }
}

TEST_F(AudioRingBufferTest, UnderrunFillsWithSilence) {
    AudioRingBuffer ringBuffer(format_, 512);

    AudioBufferListStorage writeABL(kChannels, 50, sizeof(float));
    AudioBufferListStorage readABL(kChannels, 100, sizeof(float));

    // Fill write buffer with non-zero audio
    std::fill_n(reinterpret_cast<float *>(writeABL.bufferList->mBuffers[0].mData), 50, 1.0f);
    std::fill_n(reinterpret_cast<float *>(writeABL.bufferList->mBuffers[1].mData), 50, 1.0f);

    // Fill read buffer with noise to verify clearing
    std::fill_n(reinterpret_cast<float *>(readABL.bufferList->mBuffers[0].mData), 100, -99.0f);
    std::fill_n(reinterpret_cast<float *>(readABL.bufferList->mBuffers[1].mData), 100, -99.0f);

    EXPECT_EQ(ringBuffer.write(writeABL.get(), 50), 50);

    // Request 100 frames, only 50 are available
    EXPECT_EQ(ringBuffer.read(readABL.get(), 100), 50);

    auto *ch0Read = reinterpret_cast<float *>(readABL.bufferList->mBuffers[0].mData);

    // First 50 should be valid audio
    for (std::size_t i = 0; i < 50; ++i) {
        EXPECT_FLOAT_EQ(ch0Read[i], 1.0f);
    }
    // Remaining 50 frames must be zeroed out (silence)
    for (std::size_t i = 50; i < 100; ++i) {
        EXPECT_FLOAT_EQ(ch0Read[i], 0.0f);
    }
}

TEST_F(AudioRingBufferTest, DiscardAndDiscardAll) {
    AudioRingBuffer ringBuffer(format_, 512);
    AudioBufferListStorage writeABL(kChannels, 200, sizeof(float));

    EXPECT_EQ(ringBuffer.write(writeABL.get(), 200), 200);
    EXPECT_EQ(ringBuffer.availableToRead(), 200);

    // Discard partial
    EXPECT_EQ(ringBuffer.discard(50), 50);
    EXPECT_EQ(ringBuffer.availableToRead(), 150);

    // Discard remaining
    EXPECT_EQ(ringBuffer.discardAll(), 150);
    EXPECT_EQ(ringBuffer.availableToRead(), 0);
    EXPECT_TRUE(ringBuffer.isEmpty());
}

// ============================================================================
// Multi-Threaded Concurrent SPSC Test
// ============================================================================

TEST_F(AudioRingBufferTest, ConcurrentProducerConsumerStressTest) {
    constexpr AudioRingBuffer::SizeType ringCapacity = 1024;
    constexpr AudioRingBuffer::SizeType totalFramesToSend = 1'000'000;
    constexpr AudioRingBuffer::SizeType maxChunkSize = 128;

    AudioRingBuffer ringBuffer(format_, ringCapacity);

    std::atomic<bool> producerDone{false};

    // Producer Thread
    std::thread producer([&]() {
        AudioBufferListStorage writeABL(kChannels, maxChunkSize, sizeof(float));
        AudioRingBuffer::SizeType framesSent = 0;

        while (framesSent < totalFramesToSend) {
            auto chunkSize = std::min(maxChunkSize, totalFramesToSend - framesSent);

            auto *ch0 = reinterpret_cast<float *>(writeABL.bufferList->mBuffers[0].mData);
            auto *ch1 = reinterpret_cast<float *>(writeABL.bufferList->mBuffers[1].mData);
            for (AudioRingBuffer::SizeType i = 0; i < chunkSize; ++i) {
                ch0[i] = static_cast<float>(framesSent + i);
                ch1[i] = static_cast<float>(framesSent + i) * 2.0f;
            }

            auto written = ringBuffer.write(writeABL.get(), chunkSize);
            framesSent += written;

            if (written == 0) {
                std::this_thread::yield();
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    // Consumer Thread
    std::thread consumer([&]() {
        AudioBufferListStorage readABL(kChannels, maxChunkSize, sizeof(float));
        AudioRingBuffer::SizeType framesReceived = 0;

        while (framesReceived < totalFramesToSend) {
            auto readReq = std::min(maxChunkSize, totalFramesToSend - framesReceived);
            auto readCount = ringBuffer.read(readABL.get(), readReq);

            if (readCount > 0) {
                auto *ch0 = reinterpret_cast<float *>(readABL.bufferList->mBuffers[0].mData);
                auto *ch1 = reinterpret_cast<float *>(readABL.bufferList->mBuffers[1].mData);

                for (AudioRingBuffer::SizeType i = 0; i < readCount; ++i) {
                    float expectedCh0 = static_cast<float>(framesReceived + i);
                    float expectedCh1 = expectedCh0 * 2.0f;

                    ASSERT_FLOAT_EQ(ch0[i], expectedCh0);
                    ASSERT_FLOAT_EQ(ch1[i], expectedCh1);
                }
                framesReceived += readCount;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(ringBuffer.availableToRead(), 0);
}

} // namespace test
} // namespace spsc
