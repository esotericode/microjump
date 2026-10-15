#pragma once

#include <mj/core/error.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace mj {
[[nodiscard]] std::expected<void, Error> write_png(const std::filesystem::path &path,
                                                   std::uint32_t width, std::uint32_t height,
                                                   std::span<const std::uint8_t> rgba);
} // namespace mj
