/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

#include <QtCore/QString>

#include <rpl/producer.h>

#include <memory>
#include <vector>

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct LocalBookmark {
	FullMsgId id;
	PeerId senderId = 0;
	QString peerName;
	QString senderName;
	TimeId date = 0;
	TimeId savedAt = 0;
	FullMsgId replyTo;
	bool outgoing = false;
	bool deleted = false;
	TimeId deletedAt = 0;
	QString text;
	QString media;
};

class LocalBookmarks final {
public:
	explicit LocalBookmarks(not_null<Main::Session*> session);
	~LocalBookmarks();

	[[nodiscard]] bool eligible(not_null<const HistoryItem*> item) const;
	[[nodiscard]] bool contains(FullMsgId id) const;
	[[nodiscard]] int count() const;
	[[nodiscard]] std::vector<LocalBookmark> entries() const;
	[[nodiscard]] rpl::producer<> changes() const;

	void toggle(not_null<HistoryItem*> item);
	void remove(FullMsgId id);
	void clear();
	void recordEdit(FullMsgId id, QString text);
	void markDeleted(PeerId peerId, MsgId messageId);
	void markNonChannelDeleted(MsgId messageId);
	void markPeerDeleted(PeerId peerId);

private:
	class Impl;
	const std::unique_ptr<Impl> _impl;

};

} // namespace Data
