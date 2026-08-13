/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

namespace Ui {

[[nodiscard]] int BubbleRadiusSmall();
[[nodiscard]] int BubbleRadiusLarge();

[[nodiscard]] int MsgFileThumbRadiusSmall();
[[nodiscard]] int MsgFileThumbRadiusLarge();

[[nodiscard]] bool UseSmallBubbleRadius();
void SetUseSmallBubbleRadius(bool value);
[[nodiscard]] rpl::producer<bool> UseSmallBubbleRadiusValue();

extern const char kOptionUseSmallMsgBubbleRadius[];

} // namespace Ui
