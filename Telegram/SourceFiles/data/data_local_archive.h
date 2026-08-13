/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <rpl/producer.h>

#include <memory>
#include <vector>

class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

enum class LocalArchiveRetention : uint8 {
	Off,
	Week,
	Month,
	Year,
	Forever,
};

struct LocalArchiveVersion {
	TimeId observedAt = 0;
	QString text;
	QString media;
};

struct LocalArchiveEntry {
	FullMsgId id;
	PeerId senderId = 0;
	QString senderName;
	TimeId date = 0;
	FullMsgId replyTo;
	bool outgoing = false;
	bool deleted = false;
	TimeId deletedAt = 0;
	std::vector<LocalArchiveVersion> versions;
};

struct LocalArchivePeer {
	PeerId id = 0;
	QString name;
	LocalArchiveRetention retention = LocalArchiveRetention::Off;
	int messages = 0;
	int deleted = 0;
	int edited = 0;
	qint64 bytes = 0;
};

struct LocalArchiveSearchQuery {
	QStringList terms;
	QString sender;
	bool deletedOnly = false;
	bool editedOnly = false;
	bool mediaOnly = false;
	int limit = 0;
};

struct LocalArchiveSearchResult {
	std::vector<LocalArchiveEntry> entries;
	int matched = 0;
};

class LocalArchive final {
public:
	explicit LocalArchive(not_null<Main::Session*> session);
	~LocalArchive();

	[[nodiscard]] bool ready() const;
	[[nodiscard]] rpl::producer<bool> readyValue() const;
	[[nodiscard]] rpl::producer<> changes() const;

	[[nodiscard]] bool eligible(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool enabled(PeerId peerId) const;
	[[nodiscard]] bool hasEntries(PeerId peerId) const;
	[[nodiscard]] LocalArchiveRetention retention(PeerId peerId) const;
	[[nodiscard]] std::vector<LocalArchivePeer> peers() const;

	void setRetention(
		not_null<PeerData*> peer,
		LocalArchiveRetention retention);
	void clear(PeerId peerId);
	void clearAll();
	void clearStorage();

	void markDeleted(PeerId peerId, MsgId messageId);
	void markNonChannelDeleted(MsgId messageId);
	void markPeerDeleted(PeerId peerId);
	void recordEdit(FullMsgId id, QString text, TimeId observedAt = 0);
	void forget(FullMsgId id);

	void load(
		PeerId peerId,
		Fn<void(std::vector<LocalArchiveEntry> &&)> done);
	void search(
		PeerId peerId,
		LocalArchiveSearchQuery query,
		Fn<void(LocalArchiveSearchResult &&)> done);

private:
	class Impl;
	const std::unique_ptr<Impl> _impl;

};

} // namespace Data
