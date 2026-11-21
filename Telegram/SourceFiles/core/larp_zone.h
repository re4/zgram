/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QTimeZone>
#include <rpl/producer.h>

namespace LarpZone {

void Start();
[[nodiscard]] bool UsesDeviceZone();
[[nodiscard]] QTimeZone TimeZone();
[[nodiscard]] QDateTime Now();
[[nodiscard]] QDateTime ChatTime(TimeId date);

[[nodiscard]] QByteArray SavedId();
[[nodiscard]] rpl::producer<QByteArray> SavedIdValue();
void Save(const QByteArray &id);

} // namespace LarpZone
