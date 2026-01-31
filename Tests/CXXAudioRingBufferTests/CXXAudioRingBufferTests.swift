//
// SPDX-FileCopyrightText: 2025 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXAudioRingBuffer
//

import Testing
@testable import CXXAudioRingBuffer

@Suite struct CXXAudioRingBufferTests {
    @Test func audioRingBuffer() async {
        let empty = spsc.AudioRingBuffer()
        #expect(empty.__convertToBool() == false)
        #expect(empty.capacity() == 0)
        #expect(empty.availableFrames() == 0)
        #expect(empty.freeSpace() == empty.capacity())

        var rb = spsc.AudioRingBuffer()
        let std2ch = AudioStreamBasicDescription(mSampleRate: 44100, mFormatID: kAudioFormatLinearPCM, mFormatFlags: kAudioFormatFlagsNativeFloatPacked|kAudioFormatFlagIsNonInterleaved, mBytesPerPacket: 8, mFramesPerPacket: 8, mBytesPerFrame: 8, mChannelsPerFrame: 2, mBitsPerChannel: 32, mReserved: 0)
        #expect(rb.allocate(std2ch, 512) == true)
        #expect(rb.__convertToBool() == true)
        #expect(rb.capacity() == 512)
        #expect(rb.availableFrames() == 0)
        #expect(rb.freeSpace() == rb.capacity())

        rb.deallocate()
        #expect(rb.__convertToBool() == false)
        #expect(rb.capacity() == 0)
        #expect(rb.availableFrames() == 0)
        #expect(rb.freeSpace() == rb.capacity())
    }
}
