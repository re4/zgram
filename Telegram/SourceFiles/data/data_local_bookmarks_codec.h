/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_local_bookmarks.h"

#include <QtCore/QByteArray>

#include <optional>

namespace Data::LocalBookmarksCodec {

[[nodiscard]] QByteArray Serialize(
	const std::vector<LocalBookmark> &entries);
[[nodiscard]] std::optional<std::vector<LocalBookmark>> Deserialize(
	QByteArray data);

} // namespace Data::LocalBookmarksCodec
