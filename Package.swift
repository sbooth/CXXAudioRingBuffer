// swift-tools-version: 5.9
//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXAudioRingBuffer
//

import PackageDescription

let package = Package(
    name: "CXXAudioRingBuffer",
    products: [
        .library(
            name: "CXXAudioRingBuffer",
            targets: [
                "CXXAudioRingBuffer",
            ]
        ),
    ],
    targets: [
        .target(
            name: "CXXAudioRingBuffer",
            linkerSettings: [
                .linkedFramework("CoreAudio"),
            ],
        ),
        .testTarget(
            name: "CXXAudioRingBufferTests",
            dependencies: [
                "CXXAudioRingBuffer",
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx),
            ]
        ),
    ],
    cxxLanguageStandard: .cxx17
)
