/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/power_user_settings.h"

#include "base/options.h"

namespace PowerUser {
namespace {

base::options::option<int> LayoutOption({
	.id = "power-user-layout",
	.name = "Power user layout",
	.description = "Selects comfortable or compact chat list spacing.",
	.defaultValue = int(LayoutMode::Comfortable),
	.restartRequired = true,
});

base::options::option<int> TransparencyOption({
	.id = "power-user-transparency",
	.name = "Power user window transparency",
	.description = "Applies optional transparency to Telegram windows.",
	.defaultValue = int(TransparencyMode::Off),
});

template <typename Type>
[[nodiscard]] Type Validated(int value, Type fallback) {
	return (value >= int(Type::Off) && value <= int(Type::Glass))
		? Type(value)
		: fallback;
}

[[nodiscard]] LayoutMode ValidatedLayout(int value) {
	return (value == int(LayoutMode::Compact))
		? LayoutMode::Compact
		: LayoutMode::Comfortable;
}

} // namespace

LayoutMode Layout() {
	return ValidatedLayout(LayoutOption.value());
}

void SetLayout(LayoutMode mode) {
	LayoutOption.set(int(mode));
}

rpl::producer<LayoutMode> LayoutValue() {
	return rpl::single(Layout()) | rpl::then(
		LayoutOption.changes() | rpl::map([] { return Layout(); }));
}

TransparencyMode Transparency() {
	return Validated(
		TransparencyOption.value(),
		TransparencyMode::Off);
}

void SetTransparency(TransparencyMode mode) {
	TransparencyOption.set(int(mode));
}

rpl::producer<TransparencyMode> TransparencyValue() {
	return rpl::single(Transparency()) | rpl::then(
		TransparencyOption.changes(
		) | rpl::map([] { return Transparency(); }));
}

float64 WindowOpacity(TransparencyMode mode) {
	switch (mode) {
	case TransparencyMode::Off: return 1.;
	case TransparencyMode::Subtle: return .96;
	case TransparencyMode::Glass: return .9;
	}
	Unexpected("TransparencyMode value.");
}

} // namespace PowerUser
