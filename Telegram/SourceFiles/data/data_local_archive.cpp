/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_local_archive.h"

#include "base/flat_map.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_local_archive_codec.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/cache/storage_cache_database.h"
#include "storage/storage_account.h"
#include "storage/storage_databases.h"

#include <rpl/event_stream.h>
#include <rpl/variable.h>

#include <tuple>

namespace Data {
namespace {

constexpr auto kPruneInterval = TimeId(60 * 60);
constexpr auto kPruneTimerInterval = 60 * 60 * crl::time(1000);
constexpr auto kMaxConcurrentWrites = 16;

using StoredInfo = LocalArchiveCodec::StoredInfo;
using PeerState = LocalArchiveCodec::PeerState;
using Journal = LocalArchiveCodec::Journal;

struct Snapshot {
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

struct Mutation {
	FullMsgId id;
	std::optional<Snapshot> snapshot;
	std::optional<QString> editedText;
	bool allowCreate = false;
	bool deletion = false;
	TimeId eventAt = 0;
	uint64 peerGeneration = 0;
	uint64 messageGeneration = 0;
	uint64 databaseGeneration = 0;
};

struct PendingQueue {
	std::vector<Mutation> events;
	uint64 activeSerial = 0;
	bool active = false;
};

[[nodiscard]] Storage::Cache::Key MetadataKey() {
	return {
		.high = 0,
		.low = 0x4C4F43414C415243ULL,
	};
}

[[nodiscard]] Storage::Cache::Key JournalKey() {
	return {
		.high = 0,
		.low = 0x4C4F43414C4A524EULL,
	};
}

[[nodiscard]] Storage::Cache::Key EntryKey(FullMsgId id) {
	return {
		.high = id.peer.value,
		.low = uint64(id.msg.bare),
	};
}

[[nodiscard]] bool EligiblePeer(not_null<const PeerData*> peer) {
	return peer->allowsForwarding()
		&& !peer->isRepliesChat()
		&& !peer->isVerifyCodes()
		&& !peer->isNotificationsUser()
		&& !peer->isServiceUser();
}

[[nodiscard]] std::optional<Snapshot> SnapshotFrom(
		not_null<const HistoryItem*> item) {
	const auto peer = item->history()->peer.get();
	const auto media = item->media();
	if (!EligiblePeer(peer)
		|| !IsServerMsgId(item->id)
		|| !item->isRegular()
		|| item->isService()
		|| item->isEphemeral()
		|| item->forbidsSaving()
		|| item->ttlDestroyAt()
		|| (media && media->ttlSeconds())) {
		return std::nullopt;
	}
	const auto sender = item->from();
	return Snapshot{
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

[[nodiscard]] bool ApplyMutation(
		std::optional<LocalArchiveEntry> &entry,
		const Mutation &mutation) {
	if (mutation.editedText) {
		if (!entry) {
			return false;
		}
		const auto &latest = entry->versions.back();
		if (latest.text == *mutation.editedText) {
			return false;
		}
		return LocalArchiveCodec::AppendVersion(*entry, {
			.observedAt = mutation.eventAt,
			.text = *mutation.editedText,
			.media = latest.media,
		});
	} else if (mutation.snapshot) {
		const auto &snapshot = *mutation.snapshot;
		if (!entry) {
			if (!mutation.allowCreate) {
				return false;
			}
			entry = LocalArchiveEntry{
				.id = snapshot.id,
				.senderId = snapshot.senderId,
				.senderName = snapshot.senderName,
				.date = snapshot.date,
				.replyTo = snapshot.replyTo,
				.outgoing = snapshot.outgoing,
				.versions = {
					LocalArchiveVersion{
						.observedAt = mutation.eventAt,
						.text = snapshot.text,
						.media = snapshot.media,
					},
				},
			};
			return true;
		}
		auto changed = LocalArchiveCodec::AppendVersion(*entry, {
			.observedAt = mutation.eventAt,
			.text = snapshot.text,
			.media = snapshot.media,
		});
		if (entry->senderId != snapshot.senderId
			|| entry->senderName != snapshot.senderName
			|| entry->date != snapshot.date
			|| entry->replyTo != snapshot.replyTo
			|| entry->outgoing != snapshot.outgoing) {
			entry->senderId = snapshot.senderId;
			entry->senderName = snapshot.senderName;
			entry->date = snapshot.date;
			entry->replyTo = snapshot.replyTo;
			entry->outgoing = snapshot.outgoing;
			changed = true;
		}
		return changed;
	} else if (mutation.deletion && entry) {
		return LocalArchiveCodec::MarkDeleted(*entry, mutation.eventAt);
	}
	return false;
}

} // namespace

class LocalArchive::Impl final : public base::has_weak_ptr {
public:
	explicit Impl(not_null<Main::Session*> session);
	~Impl();

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
	void recordEdit(FullMsgId id, QString text, TimeId observedAt);
	void forget(FullMsgId id);
	void load(
		PeerId peerId,
		Fn<void(std::vector<LocalArchiveEntry> &&)> done);
	void search(
		PeerId peerId,
		LocalArchiveSearchQuery query,
		Fn<void(LocalArchiveSearchResult &&)> done);

private:
	void setupEvents();
	void opened(Storage::Cache::Error error);
	void metadataLoaded(QByteArray data);
	void journalLoaded(
		base::flat_map<PeerId, PeerState> peers,
		QByteArray data);
	void finishLoading(base::flat_map<PeerId, PeerState> peers);
	void archive(not_null<HistoryItem*> item, bool allowCreate);
	void enqueue(Mutation mutation);
	void processPending(FullMsgId id);
	void processMorePending();
	void applyPending(
		FullMsgId id,
		uint64 serial,
		std::vector<Mutation> events,
		QByteArray data);
	void pendingJournaled(
		FullMsgId id,
		uint64 serial,
		uint64 peerGeneration,
		uint64 messageGeneration,
		uint64 databaseGeneration,
		LocalArchiveEntry entry,
		QByteArray serialized,
		int serializedSize,
		Storage::Cache::Error error);
	void pendingWritten(
		FullMsgId id,
		uint64 serial,
		uint64 peerGeneration,
		uint64 messageGeneration,
		uint64 databaseGeneration,
		LocalArchiveEntry entry,
		int serializedSize,
		Storage::Cache::Error error);
	void pendingIndexed(
		FullMsgId id,
		uint64 serial,
		uint64 peerGeneration,
		uint64 messageGeneration,
		uint64 databaseGeneration,
		Storage::Cache::Error error);
	void finishPending(FullMsgId id, uint64 serial);
	void saveMetadata(FnMut<void(Storage::Cache::Error)> done = nullptr);
	void saveJournal(FnMut<void(Storage::Cache::Error)> done = nullptr);
	bool prunePeer(PeerId peerId, TimeId now, bool force);
	bool pruneAll(TimeId now);
	void removeMissing(PeerId peerId, std::vector<MsgId> messageIds);
	void removeEntry(FullMsgId id);
	void removePendingFor(PeerId peerId);

	const not_null<Main::Session*> _session;
	Storage::DatabasePointer _database;
	base::flat_map<PeerId, PeerState> _peers;
	Journal _journal;
	base::flat_map<FullMsgId, PendingQueue> _pending;
	base::flat_map<FullMsgId, uint64> _messageGenerations;
	base::flat_map<
		PeerId,
		std::pair<QString, LocalArchiveRetention>> _deferredRetentions;
	std::vector<Mutation> _deferred;
	std::vector<PeerId> _deferredClears;
	std::vector<PeerId> _deferredPeerDeletions;
	std::vector<FullMsgId> _deferredRemovals;
	base::Timer _pruneTimer;
	rpl::variable<bool> _ready = false;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;
	uint64 _databaseGeneration = 0;
	uint64 _pendingSerial = 0;
	int _activePendingCount = 0;
	bool _opened = false;
	bool _clearAllRequested = false;
	bool _clearRequested = false;

};

LocalArchive::Impl::Impl(not_null<Main::Session*> session)
: _session(session)
, _database(Core::App().databases().get(
	session->local().localArchivePath(),
	session->local().localArchiveSettings())) {
	_pruneTimer.setCallback([=] {
		if (ready() && pruneAll(base::unixtime::now())) {
			saveMetadata();
			_changes.fire({});
		}
	});
	setupEvents();
	_database->open(
		session->local().cacheKey(),
		[weak = base::make_weak(this)](Storage::Cache::Error error) mutable {
			crl::on_main(weak, [=, error = std::move(error)]() mutable {
				weak->opened(std::move(error));
			});
		});
}

LocalArchive::Impl::~Impl() {
	if (_opened && !_clearRequested) {
		_database->sync();
	}
}

bool LocalArchive::Impl::ready() const {
	return _ready.current();
}

rpl::producer<bool> LocalArchive::Impl::readyValue() const {
	return _ready.value();
}

rpl::producer<> LocalArchive::Impl::changes() const {
	return _changes.events();
}

bool LocalArchive::Impl::eligible(not_null<const PeerData*> peer) const {
	return EligiblePeer(peer);
}

bool LocalArchive::Impl::enabled(PeerId peerId) const {
	const auto i = _peers.find(peerId);
	return i != end(_peers)
		&& i->second.retention != LocalArchiveRetention::Off;
}

bool LocalArchive::Impl::hasEntries(PeerId peerId) const {
	const auto i = _peers.find(peerId);
	return i != end(_peers) && !i->second.messages.empty();
}

LocalArchiveRetention LocalArchive::Impl::retention(PeerId peerId) const {
	const auto i = _peers.find(peerId);
	return (i != end(_peers))
		? i->second.retention
		: LocalArchiveRetention::Off;
}

std::vector<LocalArchivePeer> LocalArchive::Impl::peers() const {
	auto result = std::vector<LocalArchivePeer>();
	result.reserve(_peers.size());
	for (const auto &[peerId, peer] : _peers) {
		if (peer.retention == LocalArchiveRetention::Off
			&& peer.messages.empty()) {
			continue;
		}
		auto info = LocalArchivePeer{
			.id = peerId,
			.name = peer.name,
			.retention = peer.retention,
			.messages = int(peer.messages.size()),
		};
		for (const auto &message : peer.messages) {
			const auto &stored = message.second;
			info.bytes += stored.size;
			info.deleted += stored.deleted ? 1 : 0;
			info.edited += (stored.versions > 1) ? 1 : 0;
		}
		result.push_back(std::move(info));
	}
	ranges::sort(result, [](const auto &a, const auto &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return result;
}

void LocalArchive::Impl::setRetention(
		not_null<PeerData*> peer,
		LocalArchiveRetention retention) {
	if (retention != LocalArchiveRetention::Off
		&& !EligiblePeer(peer)) {
		return;
	}
	if (!ready()) {
		_deferredRetentions[peer->id] = { peer->name(), retention };
		return;
	}
	auto i = _peers.find(peer->id);
	if (i == end(_peers)) {
		i = _peers.emplace(peer->id, PeerState{
			.name = peer->name(),
			.retention = retention,
		}).first;
	} else {
		i->second.name = peer->name();
		i->second.retention = retention;
	}
	prunePeer(peer->id, base::unixtime::now(), true);
	if (i->second.retention == LocalArchiveRetention::Off
		&& i->second.messages.empty()) {
		_peers.erase(i);
	}
	saveMetadata();
	_changes.fire({});
}

void LocalArchive::Impl::clear(PeerId peerId) {
	if (!ready()) {
		_deferred.erase(ranges::remove_if(
			_deferred,
			[=](const Mutation &mutation) {
				return mutation.id.peer == peerId;
			}), end(_deferred));
		_deferredClears.push_back(peerId);
		return;
	}
	const auto i = _peers.find(peerId);
	if (i == end(_peers)) {
		return;
	}
	auto &peer = i->second;
	++peer.generation;
	removePendingFor(peerId);
	for (const auto &message : peer.messages) {
		_database->remove(EntryKey({ peerId, message.first }));
	}
	peer.messages.clear();
	if (peer.retention == LocalArchiveRetention::Off) {
		_peers.erase(i);
	}
	saveMetadata();
	_database->sync();
	_changes.fire({});
}

void LocalArchive::Impl::clearAll() {
	if (!ready()) {
		_clearAllRequested = true;
		_deferred.clear();
		_deferredClears.clear();
		_deferredPeerDeletions.clear();
		_deferredRemovals.clear();
		return;
	}
	++_databaseGeneration;
	_pending.clear();
	_activePendingCount = 0;
	_journal.clear();
	_messageGenerations.clear();
	_deferred.clear();
	_deferredClears.clear();
	_deferredPeerDeletions.clear();
	_deferredRemovals.clear();
	for (auto i = _peers.begin(); i != _peers.end();) {
		++i->second.generation;
		i->second.messages.clear();
		if (i->second.retention == LocalArchiveRetention::Off) {
			i = _peers.erase(i);
		} else {
			++i;
		}
	}
	_database->clear();
	saveMetadata();
	_changes.fire({});
}

void LocalArchive::Impl::clearStorage() {
	++_databaseGeneration;
	_clearRequested = true;
	_pruneTimer.cancel();
	_peers.clear();
	_pending.clear();
	_activePendingCount = 0;
	_journal.clear();
	_messageGenerations.clear();
	_deferred.clear();
	_deferredRetentions.clear();
	_deferredClears.clear();
	_deferredPeerDeletions.clear();
	_deferredRemovals.clear();
	_database->clear();
	_database->sync();
	_changes.fire({});
}

void LocalArchive::Impl::markDeleted(PeerId peerId, MsgId messageId) {
	auto mutation = Mutation{
		.id = { peerId, messageId },
		.deletion = true,
		.eventAt = base::unixtime::now(),
	};
	if (!ready()) {
		_deferred.push_back(std::move(mutation));
		return;
	}
	enqueue(std::move(mutation));
}

void LocalArchive::Impl::markNonChannelDeleted(MsgId messageId) {
	if (!ready()) {
		auto mutation = Mutation{
			.id = { PeerId(), messageId },
			.deletion = true,
			.eventAt = base::unixtime::now(),
		};
		_deferred.push_back(std::move(mutation));
		return;
	}
	for (const auto &[peerId, peer] : _peers) {
		if (!peerIsChannel(peerId) && peer.messages.contains(messageId)) {
			markDeleted(peerId, messageId);
			return;
		}
	}
}

void LocalArchive::Impl::markPeerDeleted(PeerId peerId) {
	if (!ready()) {
		_deferredPeerDeletions.push_back(peerId);
		return;
	}
	const auto i = _peers.find(peerId);
	if (i == end(_peers)) {
		return;
	}
	auto ids = std::vector<MsgId>();
	ids.reserve(i->second.messages.size());
	for (const auto &[messageId, info] : i->second.messages) {
		if (!info.deleted) {
			ids.push_back(messageId);
		}
	}
	for (const auto messageId : ids) {
		markDeleted(peerId, messageId);
	}
}

void LocalArchive::Impl::recordEdit(
		FullMsgId id,
		QString text,
		TimeId observedAt) {
	auto mutation = Mutation{
		.id = id,
		.editedText = std::move(text),
		.eventAt = observedAt ? observedAt : base::unixtime::now(),
	};
	if (!ready()) {
		_deferred.push_back(std::move(mutation));
		return;
	}
	enqueue(std::move(mutation));
}

void LocalArchive::Impl::forget(FullMsgId id) {
	if (!ready()) {
		_deferred.erase(ranges::remove_if(
			_deferred,
			[=](const Mutation &mutation) {
				return mutation.id == id;
			}), end(_deferred));
		_deferredRemovals.push_back(id);
		return;
	}
	removeEntry(id);
}

void LocalArchive::Impl::load(
		PeerId peerId,
		Fn<void(std::vector<LocalArchiveEntry> &&)> done) {
	search(peerId, {}, [done = std::move(done)](
			LocalArchiveSearchResult &&result) mutable {
		done(std::move(result.entries));
	});
}

void LocalArchive::Impl::search(
		PeerId peerId,
		LocalArchiveSearchQuery query,
		Fn<void(LocalArchiveSearchResult &&)> done) {
	const auto i = _peers.find(peerId);
	if (!ready() || i == end(_peers) || i->second.messages.empty()) {
		done(LocalArchiveSearchResult());
		return;
	}
	const auto searchHashes = LocalArchiveCodec::BuildQueryHashes(query.terms);
	const auto senderHashes = LocalArchiveCodec::BuildQueryHashes(
		query.sender.isEmpty()
			? QStringList()
			: QStringList{ query.sender });
	struct Candidate {
		MsgId id;
		TimeId date = 0;
	};
	auto candidates = std::vector<Candidate>();
	candidates.reserve(i->second.messages.size());
	for (const auto &[messageId, info] : i->second.messages) {
		if ((query.deletedOnly && !info.deleted)
			|| (query.editedOnly && info.versions < 2)
			|| (query.mediaOnly && !info.hasMedia)
			|| !LocalArchiveCodec::SearchMayMatch(
				info.searchHashes,
				searchHashes)
			|| !LocalArchiveCodec::SearchMayMatch(
				info.senderHashes,
				senderHashes)) {
			continue;
		}
		candidates.push_back({ messageId, info.date });
	}
	ranges::sort(candidates, [](const auto &a, const auto &b) {
		return std::tie(b.date, b.id.bare)
			< std::tie(a.date, a.id.bare);
	});
	const auto matched = int(candidates.size());
	if (query.limit > 0 && int(candidates.size()) > query.limit) {
		candidates.resize(query.limit);
	}
	if (candidates.empty()) {
		done(LocalArchiveSearchResult{ .matched = matched });
		return;
	}
	struct State {
		int left = 0;
		bool valid = true;
		LocalArchiveSearchResult result;
		std::vector<MsgId> missing;
		Fn<void(LocalArchiveSearchResult &&)> done;
	};
	const auto state = std::make_shared<State>();
	state->left = int(candidates.size());
	state->result.matched = matched;
	state->result.entries.reserve(candidates.size());
	state->done = std::move(done);
	const auto weak = base::make_weak(this);
	const auto peerGeneration = i->second.generation;
	const auto databaseGeneration = _databaseGeneration;
	for (const auto &candidate : candidates) {
		const auto messageId = candidate.id;
		_database->get(
			EntryKey({ peerId, messageId }),
			[=](QByteArray &&data) mutable {
				crl::on_main(weak, [=, data = std::move(data)]() mutable {
					const auto current = weak->_peers.find(peerId);
					if (current == end(weak->_peers)
						|| current->second.generation != peerGeneration
						|| weak->_databaseGeneration != databaseGeneration) {
						state->valid = false;
					} else {
						auto entry = LocalArchiveCodec::DeserializeEntry(
							std::move(data));
						if (!entry) {
							const auto journal = weak->_journal.find(
								FullMsgId(peerId, messageId));
							if (journal != end(weak->_journal)) {
								entry = LocalArchiveCodec::DeserializeEntry(
									journal->second);
							}
						}
						if (entry
							&& entry->id == FullMsgId(peerId, messageId)) {
							state->result.entries.push_back(std::move(*entry));
						} else {
							state->missing.push_back(messageId);
						}
					}
					if (!--state->left) {
						ranges::sort(
							state->result.entries,
							[](const auto &a, const auto &b) {
								return std::tie(b.date, b.id.msg.bare)
									< std::tie(a.date, a.id.msg.bare);
							});
						if (state->valid) {
							weak->removeMissing(
								peerId,
								std::move(state->missing));
						} else {
							state->result.entries.clear();
							state->result.matched = 0;
						}
						state->done(std::move(state->result));
					}
				});
			});
	}
}

void LocalArchive::Impl::setupEvents() {
	using Flag = MessageUpdate::Flag;
	_session->changes().messageUpdates(
		Flag::NewAdded | Flag::NewMaybeAdded | Flag::Edited
	) | rpl::on_next([=](const MessageUpdate &update) {
		const auto allowCreate = bool(
			update.flags & (Flag::NewAdded | Flag::NewMaybeAdded));
		archive(update.item, allowCreate);
	}, _lifetime);

	_session->changes().peerUpdates(
		PeerUpdate::Flag::Rights
	) | rpl::on_next([=](const PeerUpdate &update) {
		if (!update.peer->allowsForwarding()) {
			clear(update.peer->id);
			setRetention(update.peer, LocalArchiveRetention::Off);
		}
	}, _lifetime);
}

void LocalArchive::Impl::opened(Storage::Cache::Error error) {
	_opened = true;
	if (error.type != Storage::Cache::Error::Type::None) {
		finishLoading({});
		return;
	} else if (_clearRequested) {
		_database->clear();
		finishLoading({});
		return;
	}
	_database->get(
		MetadataKey(),
		[weak = base::make_weak(this)](QByteArray &&data) mutable {
			crl::on_main(weak, [=, data = std::move(data)]() mutable {
				weak->metadataLoaded(std::move(data));
			});
		});
}

void LocalArchive::Impl::metadataLoaded(QByteArray data) {
	if (auto parsed = LocalArchiveCodec::DeserializeMetadata(std::move(data))) {
		_database->get(JournalKey(), [
			weak = base::make_weak(this),
			peers = std::move(*parsed)
		](QByteArray &&journal) mutable {
			crl::on_main(weak, [
				weak,
				peers = std::move(peers),
				journal = std::move(journal)
			]() mutable {
				weak->journalLoaded(
					std::move(peers),
					std::move(journal));
			});
		});
		return;
	}
	_database->clear([weak = base::make_weak(this)](Storage::Cache::Error) {
		crl::on_main(weak, [=] {
			weak->finishLoading({});
		});
	});
}

void LocalArchive::Impl::journalLoaded(
		base::flat_map<PeerId, PeerState> peers,
		QByteArray data) {
	auto parsed = LocalArchiveCodec::DeserializeJournal(std::move(data));
	if (!parsed) {
		_database->remove(JournalKey());
		_database->sync();
		finishLoading(std::move(peers));
		return;
	}
	_journal = std::move(*parsed);
	if (_journal.empty()) {
		finishLoading(std::move(peers));
		return;
	}
	struct RecoveryState {
		bool writesSucceeded = true;
	};
	const auto recovery = std::make_shared<RecoveryState>();
	for (const auto &[id, serialized] : _journal) {
		const auto peer = peers.find(id.peer);
		const auto entry = LocalArchiveCodec::DeserializeEntry(serialized);
		if (peer == end(peers) || !entry || entry->id != id) {
			continue;
		}
		auto &stored = peer->second.messages[id.msg];
		stored.date = entry->date;
		stored.size = int(serialized.size());
		stored.versions = int(entry->versions.size());
		stored.deleted = entry->deleted;
		stored.hasMedia = ranges::any_of(
			entry->versions,
			[](const auto &version) { return !version.media.isEmpty(); });
		stored.searchHashes = LocalArchiveCodec::BuildSearchHashes(*entry);
		stored.senderHashes = LocalArchiveCodec::BuildSenderHashes(
			entry->senderName);
		_database->put(EntryKey(id), QByteArray(serialized), [=](
				Storage::Cache::Error error) {
			if (error.type != Storage::Cache::Error::Type::None) {
				recovery->writesSucceeded = false;
			}
		});
	}
	_database->put(
		MetadataKey(),
		LocalArchiveCodec::SerializeMetadata(peers),
		[weak = base::make_weak(this), recovery, peers = std::move(peers)](
				Storage::Cache::Error error) mutable {
			const auto success = recovery->writesSucceeded
				&& error.type == Storage::Cache::Error::Type::None;
			crl::on_main(weak, [
				weak,
				success,
				peers = std::move(peers)
			]() mutable {
				if (success) {
					weak->_journal.clear();
					weak->_database->remove(JournalKey());
					weak->_database->sync();
				}
				weak->finishLoading(std::move(peers));
			});
		});
	_database->sync();
}

void LocalArchive::Impl::finishLoading(
		base::flat_map<PeerId, PeerState> peers) {
	if (_clearRequested) {
		_peers.clear();
		_ready = true;
		_changes.fire({});
		return;
	}
	auto changed = false;
	if (_clearAllRequested) {
		_journal.clear();
		for (auto i = peers.begin(); i != peers.end();) {
			i->second.messages.clear();
			if (i->second.retention == LocalArchiveRetention::Off) {
				i = peers.erase(i);
			} else {
				++i;
			}
		}
		_database->clear();
		_clearAllRequested = false;
		changed = true;
	}
	_peers = std::move(peers);
	for (auto &[peerId, setting] : base::take(_deferredRetentions)) {
		auto i = _peers.find(peerId);
		if (i == end(_peers)) {
			if (setting.second == LocalArchiveRetention::Off) {
				continue;
			}
			i = _peers.emplace(peerId, PeerState()).first;
		}
		i->second.name = std::move(setting.first);
		i->second.retention = setting.second;
		changed = true;
	}
	changed = pruneAll(base::unixtime::now()) || changed;
	_ready = true;
	_pruneTimer.callEach(kPruneTimerInterval);
	if (changed) {
		saveMetadata();
	}
	for (const auto peerId : base::take(_deferredClears)) {
		clear(peerId);
	}
	for (const auto id : base::take(_deferredRemovals)) {
		removeEntry(id);
	}
	auto deferred = base::take(_deferred);
	for (auto &mutation : deferred) {
		if (mutation.deletion && !mutation.id.peer) {
			markNonChannelDeleted(mutation.id.msg);
		} else {
			enqueue(std::move(mutation));
		}
	}
	for (const auto peerId : base::take(_deferredPeerDeletions)) {
		markPeerDeleted(peerId);
	}
	_changes.fire({});
}

void LocalArchive::Impl::archive(
		not_null<HistoryItem*> item,
		bool allowCreate) {
	const auto snapshot = SnapshotFrom(item);
	if (!snapshot) {
		if (IsServerMsgId(item->id)) {
			forget(item->fullId());
		}
		return;
	}
	auto mutation = Mutation{
		.id = snapshot->id,
		.snapshot = snapshot,
		.allowCreate = allowCreate,
		.eventAt = base::unixtime::now(),
	};
	if (!ready()) {
		_deferred.push_back(std::move(mutation));
		return;
	}
	enqueue(std::move(mutation));
}

void LocalArchive::Impl::enqueue(Mutation mutation) {
	if (!mutation.id.peer || !IsServerMsgId(mutation.id.msg)) {
		return;
	}
	const auto peerIt = _peers.find(mutation.id.peer);
	if (peerIt == end(_peers)
		|| (peerIt->second.retention == LocalArchiveRetention::Off
			&& !mutation.deletion)) {
		return;
	}
	auto &peer = peerIt->second;
	auto metadataChanged = false;
	const auto publishMetadataChanges = [&] {
		if (metadataChanged) {
			saveMetadata();
			_changes.fire({});
		}
	};
	if (mutation.snapshot) {
		if (peer.name != mutation.snapshot->peerName) {
			peer.name = mutation.snapshot->peerName;
			metadataChanged = true;
		}
		metadataChanged = prunePeer(
			mutation.id.peer,
			mutation.eventAt,
			false) || metadataChanged;
		if (mutation.allowCreate) {
			if (LocalArchiveCodec::Expired(
					mutation.snapshot->date,
					peer.retention,
					mutation.eventAt)) {
				publishMetadataChanges();
				return;
			}
			peer.messages.try_emplace(mutation.id.msg, StoredInfo{
				.date = mutation.snapshot->date,
			});
		} else if (!peer.messages.contains(mutation.id.msg)) {
			publishMetadataChanges();
			return;
		}
	} else if (!peer.messages.contains(mutation.id.msg)) {
		return;
	}
	mutation.peerGeneration = peer.generation;
	mutation.messageGeneration = _messageGenerations[mutation.id];
	mutation.databaseGeneration = _databaseGeneration;
	auto &pending = _pending[mutation.id];
	pending.events.push_back(std::move(mutation));
	processPending(pending.events.back().id);
	publishMetadataChanges();
}

void LocalArchive::Impl::processPending(FullMsgId id) {
	const auto i = _pending.find(id);
	if (i == end(_pending)
		|| i->second.active
		|| i->second.events.empty()
		|| _activePendingCount >= kMaxConcurrentWrites) {
		return;
	}
	i->second.active = true;
	++_activePendingCount;
	const auto serial = ++_pendingSerial;
	i->second.activeSerial = serial;
	auto events = base::take(i->second.events);
	_database->get(EntryKey(id), [
		weak = base::make_weak(this),
		id,
		serial,
		events = std::move(events)
	](QByteArray &&data) mutable {
		crl::on_main(weak, [
			weak,
			id,
			serial,
			events = std::move(events),
			data = std::move(data)
		]() mutable {
			weak->applyPending(
				id,
				serial,
				std::move(events),
				std::move(data));
		});
	});
}

void LocalArchive::Impl::processMorePending() {
	if (_activePendingCount >= kMaxConcurrentWrites) {
		return;
	}
	for (auto i = _pending.begin();
			i != _pending.end()
				&& _activePendingCount < kMaxConcurrentWrites;
			++i) {
		if (!i->second.active && !i->second.events.empty()) {
			processPending(i->first);
		}
	}
}

void LocalArchive::Impl::applyPending(
		FullMsgId id,
		uint64 serial,
		std::vector<Mutation> events,
		QByteArray data) {
	const auto journal = _journal.find(id);
	auto entry = (journal != end(_journal))
		? LocalArchiveCodec::DeserializeEntry(journal->second)
		: LocalArchiveCodec::DeserializeEntry(std::move(data));
	if (entry && entry->id != id) {
		entry.reset();
	}
	auto changed = false;
	auto peerGeneration = uint64();
	auto messageGeneration = uint64();
	auto databaseGeneration = uint64();
	for (const auto &event : events) {
		const auto peerIt = _peers.find(id.peer);
		if (peerIt == end(_peers)
			|| peerIt->second.generation != event.peerGeneration
			|| _messageGenerations[id] != event.messageGeneration
			|| _databaseGeneration != event.databaseGeneration) {
			continue;
		}
		peerGeneration = event.peerGeneration;
		messageGeneration = event.messageGeneration;
		databaseGeneration = event.databaseGeneration;
		changed = ApplyMutation(entry, event) || changed;
	}
	if (!changed || !entry) {
		finishPending(id, serial);
		return;
	}
	auto serialized = LocalArchiveCodec::SerializeEntry(*entry);
	const auto serializedSize = int(serialized.size());
	_journal[id] = serialized;
	saveJournal([
		weak = base::make_weak(this),
		id,
		serial,
		peerGeneration,
		messageGeneration,
		databaseGeneration,
		entry = std::move(*entry),
		serialized = std::move(serialized),
		serializedSize
	](Storage::Cache::Error error) mutable {
		crl::on_main(weak, [
			weak,
			id,
			serial,
			peerGeneration,
			messageGeneration,
			databaseGeneration,
			entry = std::move(entry),
			serialized = std::move(serialized),
			serializedSize,
			error = std::move(error)
		]() mutable {
			weak->pendingJournaled(
				id,
				serial,
				peerGeneration,
				messageGeneration,
				databaseGeneration,
				std::move(entry),
				std::move(serialized),
				serializedSize,
				std::move(error));
		});
	});
}

void LocalArchive::Impl::pendingJournaled(
		FullMsgId id,
		uint64 serial,
		uint64 peerGeneration,
		uint64 messageGeneration,
		uint64 databaseGeneration,
		LocalArchiveEntry entry,
		QByteArray serialized,
		int serializedSize,
		Storage::Cache::Error error) {
	const auto peer = _peers.find(id.peer);
	const auto journal = _journal.find(id);
	if (error.type != Storage::Cache::Error::Type::None
		|| peer == end(_peers)
		|| peer->second.generation != peerGeneration
		|| _messageGenerations[id] != messageGeneration
		|| _databaseGeneration != databaseGeneration
		|| journal == end(_journal)
		|| journal->second != serialized) {
		finishPending(id, serial);
		return;
	}
	_database->put(EntryKey(id), std::move(serialized), [
		weak = base::make_weak(this),
		id,
		serial,
		peerGeneration,
		messageGeneration,
		databaseGeneration,
		entry = std::move(entry),
		serializedSize
	](Storage::Cache::Error writeError) mutable {
		crl::on_main(weak, [
			weak,
			id,
			serial,
			peerGeneration,
			messageGeneration,
			databaseGeneration,
			entry = std::move(entry),
			serializedSize,
			error = std::move(writeError)
		]() mutable {
			weak->pendingWritten(
				id,
				serial,
				peerGeneration,
				messageGeneration,
				databaseGeneration,
				std::move(entry),
				serializedSize,
				std::move(error));
		});
	});
}

void LocalArchive::Impl::pendingWritten(
		FullMsgId id,
		uint64 serial,
		uint64 peerGeneration,
		uint64 messageGeneration,
		uint64 databaseGeneration,
		LocalArchiveEntry entry,
		int serializedSize,
		Storage::Cache::Error error) {
	const auto peerIt = _peers.find(id.peer);
	if (error.type != Storage::Cache::Error::Type::None
		|| peerIt == end(_peers)
		|| peerIt->second.generation != peerGeneration
		|| _messageGenerations[id] != messageGeneration
		|| _databaseGeneration != databaseGeneration) {
		finishPending(id, serial);
		return;
	}
	auto &stored = peerIt->second.messages[id.msg];
	stored.date = entry.date;
	stored.size = serializedSize;
	stored.versions = int(entry.versions.size());
	stored.deleted = entry.deleted;
	stored.hasMedia = ranges::any_of(
		entry.versions,
		[](const auto &version) { return !version.media.isEmpty(); });
	stored.searchHashes = LocalArchiveCodec::BuildSearchHashes(entry);
	stored.senderHashes = LocalArchiveCodec::BuildSenderHashes(
		entry.senderName);
	saveMetadata([
		weak = base::make_weak(this),
		id,
		serial,
		peerGeneration,
		messageGeneration,
		databaseGeneration
	](Storage::Cache::Error metadataError) mutable {
		crl::on_main(weak, [
			weak,
			id,
			serial,
			peerGeneration,
			messageGeneration,
			databaseGeneration,
			error = std::move(metadataError)
		]() mutable {
			weak->pendingIndexed(
				id,
				serial,
				peerGeneration,
				messageGeneration,
				databaseGeneration,
				std::move(error));
		});
	});
}

void LocalArchive::Impl::pendingIndexed(
		FullMsgId id,
		uint64 serial,
		uint64 peerGeneration,
		uint64 messageGeneration,
		uint64 databaseGeneration,
		Storage::Cache::Error error) {
	const auto peer = _peers.find(id.peer);
	if (peer != end(_peers)
		&& peer->second.generation == peerGeneration
		&& _messageGenerations[id] == messageGeneration
		&& _databaseGeneration == databaseGeneration) {
		if (error.type == Storage::Cache::Error::Type::None) {
			_journal.erase(id);
			saveJournal();
		}
		_changes.fire({});
	}
	finishPending(id, serial);
}

void LocalArchive::Impl::finishPending(FullMsgId id, uint64 serial) {
	const auto i = _pending.find(id);
	if (i == end(_pending) || i->second.activeSerial != serial) {
		return;
	}
	Expects(_activePendingCount > 0);
	--_activePendingCount;
	i->second.active = false;
	if (i->second.events.empty()) {
		_pending.erase(i);
	} else {
		processPending(id);
	}
	processMorePending();
}

void LocalArchive::Impl::saveMetadata(
		FnMut<void(Storage::Cache::Error)> done) {
	if (!_opened || _clearRequested) {
		if (done) {
			done(Storage::Cache::Error{
				Storage::Cache::Error::Type::IO,
				QString(),
			});
		}
		return;
	}
	_database->put(
		MetadataKey(),
		LocalArchiveCodec::SerializeMetadata(_peers),
		std::move(done));
	_database->sync();
}

void LocalArchive::Impl::saveJournal(
		FnMut<void(Storage::Cache::Error)> done) {
	if (!_opened || _clearRequested) {
		if (done) {
			done(Storage::Cache::Error{
				Storage::Cache::Error::Type::IO,
				QString(),
			});
		}
		return;
	}
	if (_journal.empty()) {
		_database->remove(JournalKey(), std::move(done));
	} else {
		_database->put(
			JournalKey(),
			LocalArchiveCodec::SerializeJournal(_journal),
			std::move(done));
	}
	_database->sync();
}

bool LocalArchive::Impl::prunePeer(
		PeerId peerId,
		TimeId now,
		bool force) {
	const auto i = _peers.find(peerId);
	if (i == end(_peers)) {
		return false;
	}
	auto &peer = i->second;
	if (!LocalArchiveCodec::RetentionSeconds(peer.retention)) {
		return false;
	}
	if (!force && peer.lastPrunedAt + kPruneInterval > now) {
		return false;
	}
	auto changed = (peer.lastPrunedAt != now);
	peer.lastPrunedAt = now;
	auto removed = std::vector<FullMsgId>();
	auto journalChanged = false;
	for (auto j = peer.messages.begin(); j != peer.messages.end();) {
		if (LocalArchiveCodec::Expired(
				j->second.date,
				peer.retention,
				now)) {
			const auto id = FullMsgId(peerId, j->first);
			++_messageGenerations[id];
			const auto pending = _pending.find(id);
			if (pending != end(_pending)) {
				_activePendingCount -= pending->second.active ? 1 : 0;
				_pending.erase(pending);
			}
			journalChanged = bool(_journal.erase(id)) || journalChanged;
			removed.push_back(id);
			j = peer.messages.erase(j);
			changed = true;
		} else {
			++j;
		}
	}
	if (journalChanged) {
		saveJournal();
	}
	for (const auto id : removed) {
		_database->remove(EntryKey(id));
	}
	processMorePending();
	return changed;
}

void LocalArchive::Impl::removeMissing(
		PeerId peerId,
		std::vector<MsgId> messageIds) {
	const auto peer = _peers.find(peerId);
	if (peer == end(_peers)) {
		return;
	}
	auto changed = false;
	for (const auto messageId : messageIds) {
		const auto id = FullMsgId(peerId, messageId);
		if (_pending.contains(id)
			|| !peer->second.messages.contains(messageId)) {
			continue;
		}
		++_messageGenerations[id];
		peer->second.messages.erase(messageId);
		_database->remove(EntryKey(id));
		changed = true;
	}
	if (changed) {
		saveMetadata();
		_changes.fire({});
	}
}

bool LocalArchive::Impl::pruneAll(TimeId now) {
	auto changed = false;
	for (const auto &peer : _peers) {
		changed = prunePeer(peer.first, now, true) || changed;
	}
	return changed;
}

void LocalArchive::Impl::removeEntry(FullMsgId id) {
	const auto peer = _peers.find(id.peer);
	if (peer == end(_peers) || !peer->second.messages.contains(id.msg)) {
		return;
	}
	++_messageGenerations[id];
	const auto pending = _pending.find(id);
	if (pending != end(_pending)) {
		_activePendingCount -= pending->second.active ? 1 : 0;
		_pending.erase(pending);
	}
	if (_journal.erase(id)) {
		saveJournal();
	}
	peer->second.messages.erase(id.msg);
	_database->remove(EntryKey(id));
	saveMetadata();
	_changes.fire({});
	processMorePending();
}

void LocalArchive::Impl::removePendingFor(PeerId peerId) {
	auto journalChanged = false;
	for (auto i = _journal.begin(); i != _journal.end();) {
		if (i->first.peer == peerId) {
			i = _journal.erase(i);
			journalChanged = true;
		} else {
			++i;
		}
	}
	if (journalChanged) {
		saveJournal();
	}
	for (auto i = _pending.begin(); i != _pending.end();) {
		if (i->first.peer == peerId) {
			_activePendingCount -= i->second.active ? 1 : 0;
			i = _pending.erase(i);
		} else {
			++i;
		}
	}
	for (auto i = _messageGenerations.begin();
			i != _messageGenerations.end();) {
		if (i->first.peer == peerId) {
			i = _messageGenerations.erase(i);
		} else {
			++i;
		}
	}
	processMorePending();
}

LocalArchive::LocalArchive(not_null<Main::Session*> session)
: _impl(std::make_unique<Impl>(session)) {
}

LocalArchive::~LocalArchive() = default;

bool LocalArchive::ready() const {
	return _impl->ready();
}

rpl::producer<bool> LocalArchive::readyValue() const {
	return _impl->readyValue();
}

rpl::producer<> LocalArchive::changes() const {
	return _impl->changes();
}

bool LocalArchive::eligible(not_null<const PeerData*> peer) const {
	return _impl->eligible(peer);
}

bool LocalArchive::enabled(PeerId peerId) const {
	return _impl->enabled(peerId);
}

bool LocalArchive::hasEntries(PeerId peerId) const {
	return _impl->hasEntries(peerId);
}

LocalArchiveRetention LocalArchive::retention(PeerId peerId) const {
	return _impl->retention(peerId);
}

std::vector<LocalArchivePeer> LocalArchive::peers() const {
	return _impl->peers();
}

void LocalArchive::setRetention(
		not_null<PeerData*> peer,
		LocalArchiveRetention retention) {
	_impl->setRetention(peer, retention);
}

void LocalArchive::clear(PeerId peerId) {
	_impl->clear(peerId);
}

void LocalArchive::clearAll() {
	_impl->clearAll();
}

void LocalArchive::clearStorage() {
	_impl->clearStorage();
}

void LocalArchive::markDeleted(PeerId peerId, MsgId messageId) {
	_impl->markDeleted(peerId, messageId);
}

void LocalArchive::markNonChannelDeleted(MsgId messageId) {
	_impl->markNonChannelDeleted(messageId);
}

void LocalArchive::markPeerDeleted(PeerId peerId) {
	_impl->markPeerDeleted(peerId);
}

void LocalArchive::recordEdit(
		FullMsgId id,
		QString text,
		TimeId observedAt) {
	_impl->recordEdit(id, std::move(text), observedAt);
}

void LocalArchive::forget(FullMsgId id) {
	_impl->forget(id);
}

void LocalArchive::load(
		PeerId peerId,
		Fn<void(std::vector<LocalArchiveEntry> &&)> done) {
	_impl->load(peerId, std::move(done));
}

void LocalArchive::search(
		PeerId peerId,
		LocalArchiveSearchQuery query,
		Fn<void(LocalArchiveSearchResult &&)> done) {
	_impl->search(peerId, std::move(query), std::move(done));
}

} // namespace Data
