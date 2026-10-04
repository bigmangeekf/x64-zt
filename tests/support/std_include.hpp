#pragma once

// Minimal precompiled-header substitute for compiling the production filesystem
// implementation in an asset-free test. No production file implementation is mocked.
#define _CRT_SECURE_NO_WARNINGS
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
using namespace std::literals;
