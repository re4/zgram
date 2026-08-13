/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "settings/settings_type.h"

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type LocalArchiveId();

void ShowLocalArchive(
	not_null<Window::SessionController*> controller,
	PeerId peerId);
void ShowLocalArchiveRetention(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void ShowLocalBookmarks(
	not_null<Window::SessionController*> controller);

} // namespace Settings
