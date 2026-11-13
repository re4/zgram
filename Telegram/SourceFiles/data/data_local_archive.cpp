/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_local_archive.h"

#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_local_archive_store.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "storage/storage_databases.h"
#include "storage/storage_encryption.h"

namespace Data {
namespace {

[[nodiscard]] bool EligiblePeer(not_null<const PeerData*> peer) {
	return peer->allowsForwarding()
		&& !peer->isRepliesChat()
		&& !peer->isVerifyCodes()
		&& !peer->isNotificationsUser()
		&& !peer->isServiceUser();
}

[[nodiscard]] bool EligibleMessage(
		not_null<const HistoryItem*> item) {
	const auto media = item->media();
	return EligiblePeer(item->history()->peer)
		&& IsServerMsgId(item->id)
		&& item->isRegular()
		&& !item->isService()
		&& !item->isEphemeral()
		&& !item->forbidsSaving()
		&& !item->ttlDestroyAt()
		&& !(media && media->ttlSeconds());
}

[[nodiscard]] std::optional<LocalArchiveSnapshot> SnapshotFrom(
		not_null<const HistoryItem*> item) {
	if (!EligibleMessage(item)) {
		return std::nullopt;
	}
	const auto peer = item->history()->peer.get();
	const auto media = item->media();
	const auto sender = item->from();
	return LocalArchiveSnapshot{
		.id = item->fullId(),
		.senderId = sender->id,
		.senderName = sender->name(),
		.peerName = peer->name(),
		.date = item->date(),
		.replyTo = item->replyToFullId(),
		.outgoing = item->out(),
		.text = item->originalText().text,
		.media = media ? media->notificationText().text : QString(),
	};
}

} // namespace

class LocalArchive::Impl final {
public:
	explicit Impl(not_null<Main::Session*> session);

	LocalArchiveStore store;

private:
	void setupEvents();
	void archive(not_null<HistoryItem*> item, bool allowCreate);
	void archiveLoaded(PeerId peerId);

	const not_null<Main::Session*> _session;
	rpl::lifetime _lifetime;

};

LocalArchive::Impl::Impl(not_null<Main::Session*> session)
: store(
	Core::App().databases().get(
		session->local().localArchivePath(),
		session->local().localArchiveSettings()),
	session->local().cacheKey())
, _session(session) {
	setupEvents();
	store.captureRequests() | rpl::on_next([=](PeerId peerId) {
		archiveLoaded(peerId);
	}, _lifetime);
}

void LocalArchive::Impl::setupEvents() {
	using Flag = MessageUpdate::Flag;
	rpl::merge(
		_session->changes().realtimeMessageUpdates(Flag::NewAdded),
		_session->changes().realtimeMessageUpdates(Flag::NewMaybeAdded),
		_session->changes().realtimeMessageUpdates(Flag::Edited)
	) | rpl::on_next([=](const MessageUpdate &update) {
		const auto allowCreate = bool(
			update.flags & (Flag::NewAdded | Flag::NewMaybeAdded));
		archive(update.item, allowCreate);
	}, _lifetime);

	_session->changes().peerUpdates(
		PeerUpdate::Flag::Rights
	) | rpl::on_next([=](const PeerUpdate &update) {
		if (!update.peer->allowsForwarding()) {
			store.setRetention(
				update.peer->id,
				update.peer->name(),
				LocalArchiveRetention::Off);
		}
	}, _lifetime);
}

void LocalArchive::Impl::archive(
		not_null<HistoryItem*> item,
		bool allowCreate) {
	if (item->Has<HistoryServiceClearHistory>()) {
		store.markDeleted(item->history()->peer->id, item->id);
		return;
	}
	if (auto snapshot = SnapshotFrom(item)) {
		store.archive(std::move(*snapshot), allowCreate);
	}
}

void LocalArchive::Impl::archiveLoaded(PeerId peerId) {
	if (const auto history = _session->data().historyLoaded(peerId)) {
		history->enumerateMessages([=](not_null<HistoryItem*> item) {
			archive(item, true);
		});
	}
}

LocalArchive::LocalArchive(not_null<Main::Session*> session)
: _impl(std::make_unique<Impl>(session)) {
}

LocalArchive::~LocalArchive() = default;

bool LocalArchive::ready() const {
	return _impl->store.ready();
}

rpl::producer<bool> LocalArchive::readyValue() const {
	return _impl->store.readyValue();
}

rpl::producer<> LocalArchive::changes() const {
	return _impl->store.changes();
}

bool LocalArchive::eligible(not_null<const PeerData*> peer) const {
	return EligiblePeer(peer);
}

bool LocalArchive::enabled(PeerId peerId) const {
	return _impl->store.enabled(peerId);
}

bool LocalArchive::hasEntries(PeerId peerId) const {
	return _impl->store.hasEntries(peerId);
}

LocalArchiveRetention LocalArchive::retention(PeerId peerId) const {
	return _impl->store.retention(peerId);
}

std::vector<LocalArchivePeer> LocalArchive::peers() const {
	return _impl->store.peers();
}

void LocalArchive::setRetention(
		not_null<PeerData*> peer,
		LocalArchiveRetention retention) {
	if (retention != LocalArchiveRetention::Off && !EligiblePeer(peer)) {
		return;
	}
	_impl->store.setRetention(peer->id, peer->name(), retention);
}

void LocalArchive::clear(PeerId peerId) {
	_impl->store.clear(peerId);
}

void LocalArchive::clearAll() {
	_impl->store.clearAll();
}

void LocalArchive::clearStorage() {
	_impl->store.clearStorage();
}

void LocalArchive::markDeleted(PeerId peerId, MsgId messageId) {
	_impl->store.markDeleted(peerId, messageId);
}

void LocalArchive::markNonChannelDeleted(MsgId messageId) {
	_impl->store.markNonChannelDeleted(messageId);
}

void LocalArchive::markPeerDeleted(PeerId peerId, MsgId tillId) {
	_impl->store.markPeerDeleted(peerId, tillId);
}

void LocalArchive::recordEdit(
		FullMsgId id,
		QString text,
		TimeId observedAt) {
	_impl->store.recordEdit(id, std::move(text), observedAt);
}

void LocalArchive::load(
		PeerId peerId,
		Fn<void(std::vector<LocalArchiveEntry> &&)> done) {
	_impl->store.load(peerId, std::move(done));
}

void LocalArchive::search(
		PeerId peerId,
		LocalArchiveSearchQuery query,
		Fn<void(LocalArchiveSearchResult &&)> done) {
	_impl->store.search(peerId, std::move(query), std::move(done));
}

} // namespace Data
