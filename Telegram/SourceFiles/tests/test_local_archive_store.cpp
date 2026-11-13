/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_local_archive_store.h"

#include "data/data_local_archive_store.h"
#include "base/bytes.h"
#include "base/integration.h"
#include "base/unixtime.h"
#include "data/data_local_archive_codec.h"
#include "storage/storage_databases.h"
#include "storage/storage_encryption.h"
#include "ui/main_queue_processor.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSemaphore>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>

#include <rpl/never.h>

#include <iostream>

namespace crl {

rpl::producer<> on_main_update_requests() {
	return rpl::never<>();
}

} // namespace crl

namespace {

using namespace Data;

class Integration final : public base::Integration {
public:
	using base::Integration::Integration;
	void enterFromEventLoop(FnMut<void()> &&method) override {
		method();
	}
	bool logSkipDebug() override {
		return true;
	}
	void logMessageDebug(const QString &) override {
	}
	void logMessage(const QString &message) override {
		std::cerr << message.toStdString() << std::endl;
	}

};

bool Check(bool condition, const char *name) {
	if (!condition) {
		std::cerr << "Local archive store test failed: " << name << std::endl;
	}
	return condition;
}

bool Wait(Fn<bool()> predicate) {
	auto timer = QElapsedTimer();
	timer.start();
	while (!predicate()) {
		if (timer.elapsed() >= 30'000) {
			return false;
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return true;
}

Storage::EncryptionKey TestKey() {
	auto data = bytes::vector(Storage::EncryptionKey::kSize);
	bytes::set_with_const(bytes::make_span(data), bytes::type(0x42));
	return Storage::EncryptionKey(std::move(data));
}

Storage::Cache::Database::Settings TestSettings() {
	auto result = Storage::Cache::Database::Settings();
	result.trackEstimatedTime = false;
	result.totalSizeLimit = 0;
	result.totalTimeLimit = 0;
	return result;
}

LocalArchiveSnapshot Snapshot(PeerId peer, int id) {
	return {
		.id = { peer, MsgId(id) },
		.senderId = peerFromUser(UserId(11)),
		.senderName = u"Sender"_q,
		.peerName = u"Saved chat"_q,
		.date = base::unixtime::now(),
		.text = u"Saved message %1"_q.arg(id),
	};
}

std::optional<LocalArchiveSearchResult> Read(
		LocalArchiveStore &store,
		PeerId peer,
		LocalArchiveSearchQuery query = {}) {
	const auto result = std::make_shared<
		std::optional<LocalArchiveSearchResult>>();
	store.search(peer, std::move(query), [=](auto &&value) {
		*result = std::move(value);
	});
	return Wait([=] { return result->has_value(); })
		? std::move(*result)
		: std::nullopt;
}

bool Counts(LocalArchiveStore &store, PeerId peer, int count, int deleted) {
	for (const auto &info : store.peers()) {
		if (info.id == peer) {
			return info.messages == count && info.deleted == deleted;
		}
	}
	return count == 0;
}

bool Purge(Storage::Databases &databases, const QString &path, bool opening) {
	const auto peer = peerFromUser(UserId(7));
	const auto other = peerFromChannel(ChannelId(8));
	const auto makeStore = [&] {
		return std::make_unique<LocalArchiveStore>(
			databases.get(path, TestSettings()),
			TestKey());
	};
	auto store = makeStore();
	if (!opening && !Check(Wait([&] { return store->ready(); }), "open")) {
		return false;
	}
	store->setRetention(peer, u"Saved chat"_q, LocalArchiveRetention::Forever);
	store->setRetention(other, u"Other chat"_q, LocalArchiveRetention::Forever);
	for (auto id = 1; id <= 32; ++id) {
		store->archive(Snapshot(peer, id), true);
	}
	store->archive(Snapshot(other, 1), true);
	store->recordEdit({ peer, MsgId(1) }, u"Edited before purge"_q);
	auto empty = Snapshot(peer, 2);
	empty.date = 0;
	empty.text.clear();
	store->archive(std::move(empty), false);
	store->markPeerDeleted(peer, MsgId(16));
	store->markNonChannelDeleted(MsgId(20));
	store->markPeerDeleted(peer);
	store->archive(Snapshot(peer, 33), true);
	store->markPeerDeleted(peer, MsgId(32));
	const auto duringSave = Read(*store, peer);
	auto passed = Check(duringSave.has_value(), "read during queued purge");
	passed &= Check(Wait([&] {
		return store->ready() && Counts(*store, peer, 33, 32);
	}), "purge preserves all pending messages");
	auto saved = std::optional<LocalArchiveSearchResult>();
	passed &= Check(Wait([&] {
		saved = Read(*store, peer);
		return saved && saved->entries.size() == 33;
	}), "purged chat count");
	if (saved) {
		for (const auto &entry : saved->entries) {
			passed &= Check(
				!entry.versions.empty()
					&& entry.date > 0
					&& entry.versions.front().text
						== u"Saved message %1"_q.arg(entry.id.msg.bare)
					&& entry.deleted == (entry.id.msg < MsgId(33)),
				"original text survives with correct deletion state");
			if (entry.id.msg != MsgId(1)) {
				passed &= Check(entry.versions.size() == 1,
					"unavailable placeholders are not archived as edits");
			}
			if (entry.id.msg == MsgId(1)) {
				passed &= Check(entry.versions.size() == 2
					&& entry.versions.back().text == u"Edited before purge"_q,
					"edit history survives purge");
			}
		}
	}
	const auto filtered = Read(*store, peer, { .deletedOnly = true });
	passed &= Check(filtered && filtered->entries.size() == 32,
		"deleted messages remain searchable");
	const auto unaffected = Read(*store, other);
	passed &= Check(unaffected && unaffected->entries.size() == 1
		&& !unaffected->entries.front().deleted, "other chat is untouched");
	auto unavailable = Snapshot(peer, 1);
	unavailable.text.clear();
	unavailable.date = 0;
	store->archive(std::move(unavailable), false);
	store->recordEdit({ peer, MsgId(1) }, u"Late update after purge"_q);
	store->setRetention(peer, u"Saved chat"_q, LocalArchiveRetention::Off);
	store->archive(Snapshot(peer, 34), true);
	store->markPeerDeleted(peer);
	passed &= Check(Wait([&] { return Counts(*store, peer, 33, 33); }),
		"disabled archive retains existing messages and ignores new captures");
	store.reset();
	store = makeStore();
	passed &= Check(Wait([&] { return store->ready(); }), "reopen");
	const auto reopened = Read(*store, peer);
	passed &= Check(reopened && reopened->entries.size() == 33
		&& Counts(*store, peer, 33, 33), "purged archive survives reopening");
	if (reopened) {
		for (const auto &entry : reopened->entries) {
			if (entry.id.msg == MsgId(1)) {
				passed &= Check(entry.date > 0 && entry.versions.size() == 2
					&& entry.versions.back().text == u"Edited before purge"_q,
					"late unavailable updates cannot overwrite saved content");
			}
		}
	}
	store->clear(peer);
	passed &= Check(!store->hasEntries(peer), "explicit local clear works");
	const auto remaining = Read(*store, other);
	passed &= Check(remaining && remaining->entries.size() == 1,
		"explicit local clear affects only its chat");
	store->archive(Snapshot(other, 2), true);
	store->clearAll();
	const auto cleared = Read(*store, other);
	passed &= Check(cleared && cleared->entries.empty(), "clear pending saves");
	store.reset();
	store = makeStore();
	passed &= Check(Wait([&] { return store->ready(); }), "reopen cleared store");
	const auto stillCleared = Read(*store, other);
	passed &= Check(stillCleared && stillCleared->entries.empty(),
		"explicitly cleared messages do not return");
	return passed;
}

template <typename Callback>
bool DatabaseCall(Callback callback) {
	struct State {
		QSemaphore done;
		bool success = false;
	};
	const auto state = std::make_shared<State>();
	callback([=](Storage::Cache::Error error) {
		state->success = error.type == Storage::Cache::Error::Type::None;
		state->done.release();
	});
	return state->done.tryAcquire(1, 30'000) && state->success;
}

bool RecoverPurge(Storage::Databases &databases, const QString &path) {
	using namespace LocalArchiveCodec;
	const auto peer = peerFromUser(UserId(7));
	const auto entry = LocalArchiveEntry{
		.id = { peer, MsgId(1) },
		.senderName = u"Sender"_q,
		.date = base::unixtime::now(),
		.deleted = true,
		.deletedAt = base::unixtime::now(),
		.versions = { { .text = u"Saved before interrupted purge"_q } },
	};
	auto peers = Peers();
	peers[peer] = { .name = u"Saved chat"_q,
		.retention = LocalArchiveRetention::Forever };
	const auto journal = Journal{ { entry.id, SerializeEntry(entry) } };
	auto passed = true;
	{
		auto database = Storage::Cache::Database(path, TestSettings());
		passed &= Check(DatabaseCall([&](auto done) {
			database.open(TestKey(), std::move(done));
		}), "open recovery fixture");
		passed &= Check(DatabaseCall([&](auto done) {
			database.put({ 0, 0x4C4F43414C415243ULL },
				SerializeMetadata(peers), std::move(done));
		}), "write recovery metadata");
		passed &= Check(DatabaseCall([&](auto done) {
			database.put({ 0, 0x4C4F43414C4A524EULL },
				SerializeJournal(journal), std::move(done));
		}), "write interrupted purge journal");
		database.sync();
		passed &= Check(DatabaseCall([&](auto done) {
			database.close([done = std::move(done)]() mutable {
				done(Storage::Cache::Error::NoError());
			});
		}), "close recovery fixture");
	}
	for (auto attempt = 0; attempt != 2; ++attempt) {
		auto store = LocalArchiveStore(databases.get(path, TestSettings()), TestKey());
		passed &= Check(Wait([&] { return store.ready(); }), "open recovery store");
		const auto result = Read(store, peer);
		if (!result || result->entries.size() != 1) {
			std::cerr << "Recovery attempt " << attempt << " count "
				<< (result ? int(result->entries.size()) : -1) << std::endl;
		}
		passed &= Check(result && result->entries.size() == 1
			&& result->entries.front().deleted
			&& result->entries.front().versions.front().text
				== u"Saved before interrupted purge"_q,
			"interrupted purge survives recovery and second reopen");
	}
	return passed;
}

bool RetentionAfterPurge(Storage::Databases &databases, const QString &path) {
	const auto peer = peerFromUser(UserId(7));
	auto store = LocalArchiveStore(databases.get(path, TestSettings()), TestKey());
	if (!Check(Wait([&] { return store.ready(); }), "open retention store")) {
		return false;
	}
	store.setRetention(peer, u"Saved chat"_q, LocalArchiveRetention::Forever);
	store.archive(Snapshot(peer, 1), true);
	auto old = Snapshot(peer, 2);
	old.date -= 8 * 24 * 60 * 60;
	store.archive(std::move(old), true);
	store.archive(Snapshot(peer, 3), true);
	store.markPeerDeleted(peer, MsgId(2));
	auto saved = std::optional<LocalArchiveSearchResult>();
	auto passed = Check(Wait([&] {
		saved = Read(store, peer);
		return saved && saved->entries.size() == 3 && Counts(store, peer, 3, 2);
	}), "partial purge retains messages up to the boundary");
	if (saved) {
		for (const auto &entry : saved->entries) {
			passed &= Check(entry.deleted == (entry.id.msg <= MsgId(2)),
				"purge boundary is inclusive and leaves newer messages alone");
		}
	}
	store.setRetention(peer, u"Saved chat"_q, LocalArchiveRetention::Week);
	const auto pruned = Read(store, peer);
	passed &= Check(pruned && pruned->entries.size() == 2
		&& Counts(store, peer, 2, 1), "retention expires only old saved messages");
	store.clearStorage();
	passed &= Check(store.peers().empty(), "account removal clears archive");
	return passed;
}

} // namespace

bool TestLocalArchiveStore(int argc, char *argv[]) {
	auto integration = Integration(argc, argv);
	base::Integration::Set(&integration);
	auto queue = Ui::MainQueueProcessor();
	auto directory = QTemporaryDir();
	if (!Check(directory.isValid(), "temporary directory")) {
		return false;
	}
	auto databases = Storage::Databases();
	auto passed = Purge(databases, directory.filePath(u"ready"_q), false);
	passed &= Purge(databases, directory.filePath(u"opening"_q), true);
	passed &= RecoverPurge(databases, directory.filePath(u"recovery"_q));
	passed &= RetentionAfterPurge(databases, directory.filePath(u"retention"_q));
	return passed;
}
