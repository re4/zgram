/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_local_bookmarks.h"

#include "base/flat_map.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_local_bookmarks_codec.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <rpl/event_stream.h>

#include <tuple>

namespace Data {
namespace {

constexpr auto kPrefKey = "local-bookmarks-v1";
constexpr auto kMaxBookmarks = 1000;

[[nodiscard]] bool Eligible(not_null<const HistoryItem*> item) {
	const auto peer = item->history()->peer.get();
	const auto media = item->media();
	return peer->allowsForwarding()
		&& !peer->isRepliesChat()
		&& !peer->isVerifyCodes()
		&& !peer->isNotificationsUser()
		&& !peer->isServiceUser()
		&& IsServerMsgId(item->id)
		&& item->isRegular()
		&& !item->isService()
		&& !item->isEphemeral()
		&& !item->forbidsSaving()
		&& !item->ttlDestroyAt()
		&& !(media && media->ttlSeconds());
}

[[nodiscard]] LocalBookmark BookmarkFrom(
		not_null<const HistoryItem*> item,
		TimeId savedAt) {
	const auto peer = item->history()->peer.get();
	const auto sender = item->from();
	const auto media = item->media();
	return {
		.id = item->fullId(),
		.senderId = sender->id,
		.peerName = peer->name(),
		.senderName = sender->name(),
		.date = item->date(),
		.savedAt = savedAt,
		.replyTo = item->replyToFullId(),
		.outgoing = item->out(),
		.text = item->originalText().text,
		.media = media ? media->notificationText().text : QString(),
	};
}

} // namespace

class LocalBookmarks::Impl final {
public:
	explicit Impl(not_null<Main::Session*> session);

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
	void setupEvents();
	void update(not_null<HistoryItem*> item);
	void save();

	const not_null<Main::Session*> _session;
	base::flat_map<FullMsgId, LocalBookmark> _entries;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

LocalBookmarks::Impl::Impl(not_null<Main::Session*> session)
: _session(session) {
	const auto serialized = session->local().readPref<QByteArray>(kPrefKey);
	if (const auto parsed = LocalBookmarksCodec::Deserialize(serialized)) {
		for (auto entry : *parsed) {
			_entries.emplace(entry.id, std::move(entry));
		}
	} else {
		session->local().clearPref(kPrefKey);
	}
	setupEvents();
}

bool LocalBookmarks::Impl::eligible(
		not_null<const HistoryItem*> item) const {
	return Eligible(item);
}

bool LocalBookmarks::Impl::contains(FullMsgId id) const {
	return _entries.contains(id);
}

int LocalBookmarks::Impl::count() const {
	return int(_entries.size());
}

std::vector<LocalBookmark> LocalBookmarks::Impl::entries() const {
	auto result = std::vector<LocalBookmark>();
	result.reserve(_entries.size());
	for (const auto &[id, entry] : _entries) {
		result.push_back(entry);
	}
	ranges::sort(result, [](const auto &a, const auto &b) {
		return std::tie(b.savedAt, b.date, b.id.msg.bare)
			< std::tie(a.savedAt, a.date, a.id.msg.bare);
	});
	return result;
}

rpl::producer<> LocalBookmarks::Impl::changes() const {
	return _changes.events();
}

void LocalBookmarks::Impl::toggle(not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	if (_entries.contains(id)) {
		remove(id);
		return;
	} else if (!Eligible(item)) {
		return;
	}
	if (int(_entries.size()) >= kMaxBookmarks) {
		auto oldest = _entries.begin();
		auto i = oldest;
		for (++i; i != end(_entries); ++i) {
			if (i->second.savedAt < oldest->second.savedAt) {
				oldest = i;
			}
		}
		if (oldest != end(_entries)) {
			_entries.erase(oldest);
		}
	}
	auto entry = BookmarkFrom(item, base::unixtime::now());
	_entries.emplace(entry.id, std::move(entry));
	save();
	_changes.fire({});
}

void LocalBookmarks::Impl::remove(FullMsgId id) {
	if (!_entries.remove(id)) {
		return;
	}
	save();
	_changes.fire({});
}

void LocalBookmarks::Impl::clear() {
	if (_entries.empty()) {
		return;
	}
	_entries.clear();
	_session->local().clearPref(kPrefKey);
	_changes.fire({});
}

void LocalBookmarks::Impl::recordEdit(FullMsgId id, QString text) {
	const auto i = _entries.find(id);
	if (i == end(_entries) || i->second.text == text) {
		return;
	}
	i->second.text = std::move(text);
	save();
	_changes.fire({});
}

void LocalBookmarks::Impl::markDeleted(PeerId peerId, MsgId messageId) {
	const auto i = _entries.find(FullMsgId(peerId, messageId));
	if (i == end(_entries) || i->second.deleted) {
		return;
	}
	i->second.deleted = true;
	i->second.deletedAt = base::unixtime::now();
	save();
	_changes.fire({});
}

void LocalBookmarks::Impl::markNonChannelDeleted(MsgId messageId) {
	for (auto &[id, entry] : _entries) {
		if (!peerIsChannel(id.peer) && id.msg == messageId) {
			markDeleted(id.peer, messageId);
			return;
		}
	}
}

void LocalBookmarks::Impl::markPeerDeleted(PeerId peerId) {
	auto changed = false;
	const auto now = base::unixtime::now();
	for (auto &[id, entry] : _entries) {
		if (id.peer == peerId && !entry.deleted) {
			entry.deleted = true;
			entry.deletedAt = now;
			changed = true;
		}
	}
	if (changed) {
		save();
		_changes.fire({});
	}
}

void LocalBookmarks::Impl::setupEvents() {
	using Flag = MessageUpdate::Flag;
	_session->changes().messageUpdates(
		Flag::NewMaybeAdded | Flag::Edited
	) | rpl::on_next([=](const MessageUpdate &update) {
		if (_entries.contains(update.item->fullId())) {
			this->update(update.item);
		}
	}, _lifetime);

	_session->changes().peerUpdates(
		PeerUpdate::Flag::Rights
	) | rpl::on_next([=](const PeerUpdate &update) {
		if (update.peer->allowsForwarding()) {
			return;
		}
		auto changed = false;
		for (auto i = _entries.begin(); i != _entries.end();) {
			if (i->first.peer == update.peer->id) {
				i = _entries.erase(i);
				changed = true;
			} else {
				++i;
			}
		}
		if (changed) {
			save();
			_changes.fire({});
		}
	}, _lifetime);
}

void LocalBookmarks::Impl::update(not_null<HistoryItem*> item) {
	const auto i = _entries.find(item->fullId());
	if (i == end(_entries)) {
		return;
	} else if (!Eligible(item)) {
		remove(item->fullId());
		return;
	}
	const auto savedAt = i->second.savedAt;
	const auto deleted = i->second.deleted;
	const auto deletedAt = i->second.deletedAt;
	auto updated = BookmarkFrom(item, savedAt);
	updated.deleted = deleted;
	updated.deletedAt = deletedAt;
	if (i->second.text == updated.text
		&& i->second.media == updated.media
		&& i->second.senderName == updated.senderName
		&& i->second.peerName == updated.peerName
		&& i->second.replyTo == updated.replyTo) {
		return;
	}
	i->second = std::move(updated);
	save();
	_changes.fire({});
}

void LocalBookmarks::Impl::save() {
	if (_entries.empty()) {
		_session->local().clearPref(kPrefKey);
		return;
	}
	_session->local().writePref<QByteArray>(
		kPrefKey,
		LocalBookmarksCodec::Serialize(entries()));
}

LocalBookmarks::LocalBookmarks(not_null<Main::Session*> session)
: _impl(std::make_unique<Impl>(session)) {
}

LocalBookmarks::~LocalBookmarks() = default;

bool LocalBookmarks::eligible(not_null<const HistoryItem*> item) const {
	return _impl->eligible(item);
}

bool LocalBookmarks::contains(FullMsgId id) const {
	return _impl->contains(id);
}

int LocalBookmarks::count() const {
	return _impl->count();
}

std::vector<LocalBookmark> LocalBookmarks::entries() const {
	return _impl->entries();
}

rpl::producer<> LocalBookmarks::changes() const {
	return _impl->changes();
}

void LocalBookmarks::toggle(not_null<HistoryItem*> item) {
	_impl->toggle(item);
}

void LocalBookmarks::remove(FullMsgId id) {
	_impl->remove(id);
}

void LocalBookmarks::clear() {
	_impl->clear();
}

void LocalBookmarks::recordEdit(FullMsgId id, QString text) {
	_impl->recordEdit(id, std::move(text));
}

void LocalBookmarks::markDeleted(PeerId peerId, MsgId messageId) {
	_impl->markDeleted(peerId, messageId);
}

void LocalBookmarks::markNonChannelDeleted(MsgId messageId) {
	_impl->markNonChannelDeleted(messageId);
}

void LocalBookmarks::markPeerDeleted(PeerId peerId) {
	_impl->markPeerDeleted(peerId);
}

} // namespace Data
