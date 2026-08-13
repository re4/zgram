/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/bytes.h"
#include "data/data_local_archive_codec.h"
#include "data/data_local_bookmarks_codec.h"
#include "storage/cache/storage_cache_database.h"
#include "storage/storage_encryption.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDataStream>
#include <QtCore/QSemaphore>
#include <QtCore/QTemporaryDir>

#include <iostream>
#include <memory>

namespace {

template <typename Value>
struct AsyncResult {
	QSemaphore done;
	Value value = {};
};

bool Check(bool condition, const char *name) {
	if (!condition) {
		std::cerr << "Local archive test failed: " << name << std::endl;
	}
	return condition;
}

Storage::EncryptionKey TestKey(
		bytes::type value = bytes::type(0x42)) {
	auto data = bytes::vector(Storage::EncryptionKey::kSize);
	bytes::set_with_const(bytes::make_span(data), value);
	return Storage::EncryptionKey(std::move(data));
}

bool Open(
		Storage::Cache::Database &database,
		Storage::EncryptionKey key) {
	const auto state = std::make_shared<AsyncResult<bool>>();
	database.open(std::move(key), [=](Storage::Cache::Error error) {
		state->value = (error.type == Storage::Cache::Error::Type::None);
		state->done.release();
	});
	return state->done.tryAcquire(1, 30'000) && state->value;
}

bool RejectWrongKey(
		Storage::Cache::Database &database,
		Storage::EncryptionKey key) {
	const auto state = std::make_shared<AsyncResult<bool>>();
	database.open(std::move(key), [=](Storage::Cache::Error error) {
		state->value = (error.type == Storage::Cache::Error::Type::WrongKey);
		state->done.release();
	});
	return state->done.tryAcquire(1, 30'000) && state->value;
}

bool Put(
		Storage::Cache::Database &database,
		Storage::Cache::Key key,
		QByteArray value) {
	const auto state = std::make_shared<AsyncResult<bool>>();
	database.put(key, std::move(value), [=](Storage::Cache::Error error) {
		state->value = (error.type == Storage::Cache::Error::Type::None);
		state->done.release();
	});
	return state->done.tryAcquire(1, 30'000) && state->value;
}

std::optional<QByteArray> Get(
		Storage::Cache::Database &database,
		Storage::Cache::Key key) {
	const auto state = std::make_shared<AsyncResult<QByteArray>>();
	database.get(key, [=](QByteArray &&value) {
		state->value = std::move(value);
		state->done.release();
	});
	return state->done.tryAcquire(1, 30'000)
		? std::make_optional(std::move(state->value))
		: std::nullopt;
}

bool Remove(
		Storage::Cache::Database &database,
		Storage::Cache::Key key) {
	const auto state = std::make_shared<AsyncResult<bool>>();
	database.remove(key, [=](Storage::Cache::Error error) {
		state->value = (error.type == Storage::Cache::Error::Type::None);
		state->done.release();
	});
	return state->done.tryAcquire(1, 30'000) && state->value;
}

bool Clear(Storage::Cache::Database &database) {
	const auto state = std::make_shared<AsyncResult<bool>>();
	database.clear([=](Storage::Cache::Error error) {
		state->value = (error.type == Storage::Cache::Error::Type::None);
		state->done.release();
	});
	return state->done.tryAcquire(1, 30'000) && state->value;
}

bool Close(Storage::Cache::Database &database) {
	const auto state = std::make_shared<AsyncResult<bool>>();
	database.close([=] {
		state->value = true;
		state->done.release();
	});
	return state->done.tryAcquire(1, 30'000) && state->value;
}

QByteArray MetadataV1(PeerId peerId, MsgId messageId) {
	auto result = QByteArray();
	auto writer = QDataStream(&result, QIODevice::WriteOnly);
	writer.setVersion(QDataStream::Qt_5_1);
	writer
		<< qint32(1)
		<< quint32(1)
		<< quint64(peerId.value)
		<< qint32(Data::LocalArchiveRetention::Month)
		<< u"Migrated chat"_q
		<< quint32(1)
		<< qint64(messageId.bare)
		<< qint32(1'700'000'000)
		<< qint32(256)
		<< qint32(2)
		<< quint8(1);
	return result;
}

} // namespace

int main(int argc, char *argv[]) {
	using namespace Data;
	using namespace Data::LocalArchiveCodec;

	auto application = QCoreApplication(argc, argv);
	Q_UNUSED(application);
	const auto peerId = peerFromChat(ChatId(7));
	const auto senderId = peerFromUser(UserId(11));
	auto entry = LocalArchiveEntry{
		.id = FullMsgId(peerId, MsgId(42)),
		.senderId = senderId,
		.senderName = u"Sender"_q,
		.date = 1'700'000'000,
		.replyTo = FullMsgId(peerId, MsgId(40)),
		.outgoing = true,
		.versions = {
			LocalArchiveVersion{
				.observedAt = 1'700'000'001,
				.text = u"Before"_q,
				.media = u"Photo"_q,
			},
		},
	};

	auto passed = true;
	const auto encoded = SerializeEntry(entry);
	const auto decoded = DeserializeEntry(encoded);
	passed &= Check(decoded.has_value(), "record round trip");
	if (decoded) {
		passed &= Check(decoded->id == entry.id, "message identity");
		passed &= Check(decoded->senderId == senderId, "sender identity");
		passed &= Check(decoded->replyTo == entry.replyTo, "reply identity");
		passed &= Check(decoded->outgoing, "outgoing flag");
		passed &= Check(
			decoded->versions.front().text == u"Before"_q,
			"message text");
	}

	passed &= Check(!DeserializeEntry(QByteArray()), "empty interrupted write");
	auto truncated = encoded;
	truncated.chop(3);
	passed &= Check(!DeserializeEntry(truncated), "truncated interrupted write");
	auto journal = Journal();
	journal.emplace(entry.id, encoded);
	const auto journalRoundTrip = DeserializeJournal(SerializeJournal(journal));
	passed &= Check(
		journalRoundTrip
			&& journalRoundTrip->contains(entry.id)
			&& journalRoundTrip->find(entry.id)->second == encoded,
		"write journal round trip");
	auto truncatedJournal = SerializeJournal(journal);
	truncatedJournal.chop(3);
	passed &= Check(
		!DeserializeJournal(truncatedJournal),
		"truncated write journal");

	passed &= Check(!AppendVersion(entry, {
		.observedAt = 1'700'000'002,
		.text = u"Before"_q,
		.media = u"Photo"_q,
	}), "edit deduplication");
	passed &= Check(AppendVersion(entry, {
		.observedAt = 1'700'000'003,
		.text = u"After"_q,
		.media = u"Photo"_q,
	}), "edit version append");
	passed &= Check(entry.versions.size() == 2, "edit version count");

	passed &= Check(MarkDeleted(entry, 1'700'000'004), "first deletion");
	passed &= Check(!MarkDeleted(entry, 1'700'000'005), "deletion deduplication");
	passed &= Check(
		entry.deletedAt == 1'700'000'004,
		"deletion timestamp retention");

	passed &= Check(
		RetentionSeconds(LocalArchiveRetention::Week) == 7 * 24 * 60 * 60,
		"seven day retention");
	passed &= Check(
		RetentionSeconds(LocalArchiveRetention::Month) == 30 * 24 * 60 * 60,
		"thirty day retention");
	passed &= Check(
		RetentionSeconds(LocalArchiveRetention::Year) == 365 * 24 * 60 * 60,
		"one year retention");
	passed &= Check(
		RetentionSeconds(LocalArchiveRetention::Forever) == 0,
		"forever retention");
	const auto retentionNow = TimeId(1'800'000'000);
	passed &= Check(Expired(
		retentionNow - 8 * 24 * 60 * 60,
		LocalArchiveRetention::Week,
		retentionNow), "expired retention cleanup");
	passed &= Check(!Expired(
		retentionNow - 6 * 24 * 60 * 60,
		LocalArchiveRetention::Week,
		retentionNow), "retained recent message");
	passed &= Check(!Expired(
		retentionNow - 400 * 24 * 60 * 60,
		LocalArchiveRetention::Forever,
		retentionNow), "forever skips cleanup");
	passed &= Check(!Expired(
		retentionNow - 400 * 24 * 60 * 60,
		LocalArchiveRetention::Off,
		retentionNow), "disabled retention skips cleanup");

	auto metadata = Peers();
	auto peerState = PeerState{
		.name = u"First chat"_q,
		.retention = LocalArchiveRetention::Year,
		.lastPrunedAt = 1'700'000'010,
	};
	peerState.messages.emplace(MsgId(42), StoredInfo{
		.date = entry.date,
		.size = encoded.size(),
		.versions = int(entry.versions.size()),
		.deleted = entry.deleted,
		.hasMedia = true,
		.searchHashes = BuildSearchHashes(entry),
		.senderHashes = BuildSenderHashes(entry.senderName),
	});
	metadata.emplace(peerId, std::move(peerState));
	const auto metadataRoundTrip = DeserializeMetadata(
		SerializeMetadata(metadata));
	passed &= Check(
		metadataRoundTrip && metadataRoundTrip->size() == 1,
		"metadata round trip");
	if (metadataRoundTrip) {
		const auto i = metadataRoundTrip->find(peerId);
		passed &= Check(i != end(*metadataRoundTrip), "metadata peer identity");
		if (i != end(*metadataRoundTrip)) {
			passed &= Check(
				i->second.messages.contains(MsgId(42)),
				"metadata message identity");
			passed &= Check(
				i->second.retention == LocalArchiveRetention::Year,
				"metadata retention");
			const auto stored = i->second.messages.find(MsgId(42));
			passed &= Check(
				stored != end(i->second.messages)
					&& stored->second.hasMedia,
				"metadata media index");
			passed &= Check(
				stored != end(i->second.messages)
					&& SearchMayMatch(
						stored->second.searchHashes,
						BuildQueryHashes({ u"after"_q })),
				"persistent full text index");
			passed &= Check(
				stored != end(i->second.messages)
					&& !SearchMayMatch(
						stored->second.searchHashes,
						BuildQueryHashes({ u"missing"_q })),
				"persistent full text rejection");
		}
	}
	auto truncatedMetadata = SerializeMetadata(metadata);
	truncatedMetadata.chop(3);
	passed &= Check(
		!DeserializeMetadata(truncatedMetadata),
		"truncated metadata write");
	auto pendingMetadata = metadata;
	pendingMetadata.begin()->second.messages.emplace(MsgId(44), StoredInfo{
		.date = entry.date,
	});
	const auto pendingRoundTrip = DeserializeMetadata(
		SerializeMetadata(pendingMetadata));
	passed &= Check(
		pendingRoundTrip
			&& pendingRoundTrip->begin()->second.messages.size() == 1,
		"unfinished record omitted from metadata");
	const auto migrated = DeserializeMetadata(MetadataV1(peerId, MsgId(42)));
	passed &= Check(migrated && migrated->size() == 1, "version one migration");
	if (migrated) {
		const auto i = migrated->find(peerId);
		passed &= Check(i != end(*migrated), "migrated peer identity");
		if (i != end(*migrated)) {
			passed &= Check(
				i->second.lastPrunedAt == 0,
				"migration default value");
			const auto message = i->second.messages.find(MsgId(42));
			passed &= Check(
				message != end(i->second.messages)
					&& message->second.deleted,
				"migration deletion state");
			passed &= Check(
				message != end(i->second.messages)
					&& message->second.searchHashes.empty(),
				"migration search fallback");
		}
	}

	const auto bookmark = LocalBookmark{
		.id = FullMsgId(peerId, MsgId(52)),
		.senderId = senderId,
		.peerName = u"Bookmarked chat"_q,
		.senderName = u"Bookmarked sender"_q,
		.date = 1'700'000'020,
		.savedAt = 1'700'000'030,
		.replyTo = FullMsgId(peerId, MsgId(50)),
		.outgoing = true,
		.deleted = true,
		.deletedAt = 1'700'000'040,
		.text = u"Persistent bookmark text"_q,
		.media = u"Document"_q,
	};
	const auto bookmarksEncoded = LocalBookmarksCodec::Serialize({ bookmark });
	const auto bookmarksDecoded = LocalBookmarksCodec::Deserialize(
		bookmarksEncoded);
	passed &= Check(
		bookmarksDecoded && bookmarksDecoded->size() == 1,
		"bookmark round trip");
	if (bookmarksDecoded && !bookmarksDecoded->empty()) {
		const auto &decodedBookmark = bookmarksDecoded->front();
		passed &= Check(
			decodedBookmark.id == bookmark.id,
			"bookmark message identity");
		passed &= Check(
			decodedBookmark.replyTo == bookmark.replyTo,
			"bookmark reply identity");
		passed &= Check(
			decodedBookmark.deleted
				&& decodedBookmark.deletedAt == bookmark.deletedAt,
			"bookmark deletion retention");
		passed &= Check(
			decodedBookmark.text == bookmark.text
				&& decodedBookmark.media == bookmark.media,
			"bookmark snapshot retention");
	}
	auto truncatedBookmarks = bookmarksEncoded;
	truncatedBookmarks.chop(3);
	passed &= Check(
		!LocalBookmarksCodec::Deserialize(truncatedBookmarks),
		"truncated bookmark write");
	passed &= Check(
		!LocalBookmarksCodec::Deserialize(
			LocalBookmarksCodec::Serialize({ bookmark, bookmark })),
		"duplicate bookmark rejection");

	auto directory = QTemporaryDir();
	passed &= Check(directory.isValid(), "temporary database directory");
	if (directory.isValid()) {
		auto settings = Storage::Cache::Database::Settings();
		settings.clearOnWrongKey = false;
		settings.trackEstimatedTime = false;
		settings.totalSizeLimit = 0;
		settings.totalTimeLimit = 0;
		auto database = Storage::Cache::Database(
			directory.filePath(u"archive"_q),
			settings);
		const auto firstKey = Storage::Cache::Key{
			.high = peerId.value,
			.low = 42,
		};
		const auto secondPeerId = peerFromChat(ChatId(8));
		const auto secondKey = Storage::Cache::Key{
			.high = secondPeerId.value,
			.low = 43,
		};
		const auto journalKey = Storage::Cache::Key{
			.high = 0,
			.low = 0x4C4F43414C4A524EULL,
		};
		auto second = entry;
		second.id = FullMsgId(secondPeerId, MsgId(43));

		passed &= Check(Open(database, TestKey()), "encrypted database open");
		passed &= Check(
			Put(database, firstKey, SerializeEntry(entry)),
			"first peer write");
		passed &= Check(
			Put(database, secondKey, SerializeEntry(second)),
			"second peer write");
		passed &= Check(
			Put(database, journalKey, SerializeJournal(journal)),
			"write-ahead journal write");
		database.sync();
		passed &= Check(Close(database), "encrypted database close");
		passed &= Check(
			RejectWrongKey(database, TestKey(bytes::type(0x24))),
			"encrypted database rejects wrong key");
		passed &= Check(Open(database, TestKey()), "encrypted database reopen");
		const auto persisted = Get(database, firstKey);
		passed &= Check(
			persisted && DeserializeEntry(*persisted).has_value(),
			"archive persistence after reopen");
		const auto persistedJournal = Get(database, journalKey);
		passed &= Check(
			persistedJournal
				&& DeserializeJournal(*persistedJournal).has_value(),
			"write-ahead journal persistence");
		passed &= Check(
			Remove(database, journalKey),
			"write-ahead journal cleanup");
		passed &= Check(Remove(database, firstKey), "single peer clear");
		const auto firstAfterClear = Get(database, firstKey);
		const auto secondAfterClear = Get(database, secondKey);
		passed &= Check(
			firstAfterClear && firstAfterClear->isEmpty(),
			"cleared peer is empty");
		passed &= Check(
			secondAfterClear && DeserializeEntry(*secondAfterClear).has_value(),
			"other peer remains isolated");
		passed &= Check(Clear(database), "account archive clear");
		const auto secondAfterAccountClear = Get(database, secondKey);
		passed &= Check(
			secondAfterAccountClear && secondAfterAccountClear->isEmpty(),
			"account removal clear");
		passed &= Check(Close(database), "final database close");
	}

	return passed ? 0 : 1;
}
