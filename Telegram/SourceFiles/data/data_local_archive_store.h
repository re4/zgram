/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_local_archive.h"

namespace Storage {
class DatabasePointer;
class EncryptionKey;
} // namespace Storage

namespace Data {

struct LocalArchiveSnapshot {
	FullMsgId id;
	PeerId senderId = 0;
	QString senderName;
	QString peerName;
	TimeId date = 0;
	FullMsgId replyTo;
	bool outgoing = false;
	QString text;
	QString media;
};

class LocalArchiveStore final {
public:
	explicit LocalArchiveStore(
		Storage::DatabasePointer database,
		Storage::EncryptionKey key);
	~LocalArchiveStore();

	[[nodiscard]] bool ready() const;
	[[nodiscard]] rpl::producer<bool> readyValue() const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] rpl::producer<PeerId> captureRequests() const;
	[[nodiscard]] bool enabled(PeerId peerId) const;
	[[nodiscard]] bool hasEntries(PeerId peerId) const;
	[[nodiscard]] LocalArchiveRetention retention(PeerId peerId) const;
	[[nodiscard]] std::vector<LocalArchivePeer> peers() const;

	void setRetention(
		PeerId peerId,
		QString name,
		LocalArchiveRetention retention);
	void archive(LocalArchiveSnapshot snapshot, bool allowCreate);
	void clear(PeerId peerId);
	void clearAll();
	void clearStorage();
	void markDeleted(PeerId peerId, MsgId messageId);
	void markNonChannelDeleted(MsgId messageId);
	void markPeerDeleted(PeerId peerId, MsgId tillId = ServerMaxMsgId);
	void recordEdit(FullMsgId id, QString text, TimeId observedAt = 0);
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
