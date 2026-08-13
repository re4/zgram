/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <rpl/producer.h>

namespace PowerUser {

enum class LayoutMode {
	Comfortable,
	Compact,
};

enum class TransparencyMode {
	Off,
	Subtle,
	Glass,
};

[[nodiscard]] LayoutMode Layout();
void SetLayout(LayoutMode mode);
[[nodiscard]] rpl::producer<LayoutMode> LayoutValue();

[[nodiscard]] TransparencyMode Transparency();
void SetTransparency(TransparencyMode mode);
[[nodiscard]] rpl::producer<TransparencyMode> TransparencyValue();
[[nodiscard]] float64 WindowOpacity(TransparencyMode mode);

} // namespace PowerUser
