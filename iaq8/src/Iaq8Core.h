#pragma once

// Anchor of the iaq8_core static library (a static library needs at least one
// source). Later tasks add their own sources under src/.
namespace iaq8
{
const char* coreVersion() noexcept;
}
