/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_local_archive_codec.h"

#include <QtCore/QDataStream>

#include <algorithm>

namespace Data::LocalArchiveCodec {
namespace {

constexpr auto kMetadataVersion = qint32(3);
constexpr auto kMinimumMetadataVersion = qint32(1);
constexpr auto kJournalVersion = qint32(1);
constexpr auto kEntryVersion = qint32(1);
constexpr auto kMaxPeers = quint32(100'000);
constexpr auto kMaxMessagesPerPeer = quint32(10'000'000);
constexpr auto kMaxJournalEntries = quint32(100'000);
constexpr auto kMaxVersionsPerMessage = 512;
constexpr auto kMaxSearchHashesPerMessage = quint32(4096);

[[nodiscard]] quint32 SearchHash(QStringView text) {
	auto result = quint32(2166136261U);
	for (const auto character : text) {
		const auto value = character.unicode();
		result = (result ^ quint8(value & 0xFF)) * 16777619U;
		result = (result ^ quint8(value >> 8)) * 16777619U;
	}
	return result;
}

[[nodiscard]] bool AddSearchHashes(
		std::vector<quint32> &result,
		const QString &text) {
	const auto folded = text.toCaseFolded();
	const auto size = int(folded.size());
	if (size < 3) {
		return true;
	}
	result.reserve(std::min(
		int(kMaxSearchHashesPerMessage) + 1,
		int(result.size()) + size - 2));
	for (auto i = 0; i + 2 < size; ++i) {
		result.push_back(SearchHash(QStringView(folded).mid(i, 3)));
		if (result.size() > kMaxSearchHashesPerMessage) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] std::vector<quint32> FinishSearchHashes(
		std::vector<quint32> result) {
	ranges::sort(result);
	result.erase(std::unique(begin(result), end(result)), end(result));
	return (result.size() <= kMaxSearchHashesPerMessage)
		? std::move(result)
		: std::vector<quint32>();
}

[[nodiscard]] bool ValidRetention(qint32 value) {
	return value >= qint32(LocalArchiveRetention::Off)
		&& value <= qint32(LocalArchiveRetention::Forever);
}

} // namespace

QByteArray SerializeMetadata(const Peers &peers) {
	auto result = QByteArray();
	auto writer = QDataStream(&result, QIODevice::WriteOnly);
	writer.setVersion(QDataStream::Qt_5_1);
	writer << kMetadataVersion << quint32(peers.size());
	for (const auto &[peerId, peer] : peers) {
		auto messagesCount = quint32();
		for (const auto &message : peer.messages) {
			const auto &info = message.second;
			messagesCount += (info.size > 0 && info.versions > 0) ? 1 : 0;
		}
		writer
			<< quint64(peerId.value)
			<< qint32(peer.retention)
			<< peer.name
			<< messagesCount;
		for (const auto &[messageId, info] : peer.messages) {
			if (info.size <= 0 || info.versions <= 0) {
				continue;
			}
			writer
				<< qint64(messageId.bare)
				<< qint32(info.date)
				<< qint32(info.size)
				<< qint32(info.versions)
				<< quint8(info.deleted ? 1 : 0)
				<< quint8(info.hasMedia ? 1 : 0)
				<< quint32(info.searchHashes.size());
			for (const auto hash : info.searchHashes) {
				writer << hash;
			}
			writer << quint32(info.senderHashes.size());
			for (const auto hash : info.senderHashes) {
				writer << hash;
			}
		}
		writer << qint32(peer.lastPrunedAt);
	}
	return result;
}

std::optional<Peers> DeserializeMetadata(QByteArray data) {
	if (data.isEmpty()) {
		return Peers();
	}
	auto reader = QDataStream(&data, QIODevice::ReadOnly);
	reader.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto peersCount = quint32();
	reader >> version >> peersCount;
	if (reader.status() != QDataStream::Ok
		|| version < kMinimumMetadataVersion
		|| version > kMetadataVersion
		|| peersCount > kMaxPeers) {
		return std::nullopt;
	}
	auto result = Peers();
	for (auto peerIndex = quint32(); peerIndex != peersCount; ++peerIndex) {
		auto peerRaw = quint64();
		auto retentionRaw = qint32();
		auto name = QString();
		auto messagesCount = quint32();
		reader >> peerRaw >> retentionRaw >> name >> messagesCount;
		if (reader.status() != QDataStream::Ok
			|| !peerRaw
			|| !ValidRetention(retentionRaw)
			|| messagesCount > kMaxMessagesPerPeer) {
			return std::nullopt;
		}
		auto state = PeerState{
			.name = std::move(name),
			.retention = LocalArchiveRetention(retentionRaw),
		};
		for (auto messageIndex = quint32();
				messageIndex != messagesCount;
				++messageIndex) {
			auto messageRaw = qint64();
			auto date = qint32();
			auto size = qint32();
			auto versions = qint32();
			auto deleted = quint8();
			auto hasMedia = quint8();
			auto searchHashes = std::vector<quint32>();
			auto senderHashes = std::vector<quint32>();
			reader >> messageRaw >> date >> size >> versions >> deleted;
			if (reader.status() != QDataStream::Ok
				|| !IsServerMsgId(MsgId(messageRaw))
				|| date < 0
				|| size <= 0
				|| versions <= 0
				|| versions > kMaxVersionsPerMessage
				|| deleted > 1) {
				return std::nullopt;
			}
			if (version >= 3) {
				auto searchCount = quint32();
				reader >> hasMedia >> searchCount;
				if (reader.status() != QDataStream::Ok
					|| hasMedia > 1
					|| searchCount > kMaxSearchHashesPerMessage) {
					return std::nullopt;
				}
				searchHashes.resize(searchCount);
				for (auto &hash : searchHashes) {
					reader >> hash;
				}
				auto senderCount = quint32();
				reader >> senderCount;
				if (reader.status() != QDataStream::Ok
					|| senderCount > kMaxSearchHashesPerMessage) {
					return std::nullopt;
				}
				senderHashes.resize(senderCount);
				for (auto &hash : senderHashes) {
					reader >> hash;
				}
				if (reader.status() != QDataStream::Ok
					|| !ranges::is_sorted(searchHashes)
					|| !ranges::is_sorted(senderHashes)) {
					return std::nullopt;
				}
			}
			if (!state.messages.emplace(MsgId(messageRaw), StoredInfo{
				.date = date,
				.size = size,
				.versions = versions,
				.deleted = (deleted != 0),
				.hasMedia = (hasMedia != 0),
				.searchHashes = std::move(searchHashes),
				.senderHashes = std::move(senderHashes),
			}).second) {
				return std::nullopt;
			}
		}
		if (version >= 2) {
			auto lastPrunedAt = qint32();
			reader >> lastPrunedAt;
			if (reader.status() != QDataStream::Ok || lastPrunedAt < 0) {
				return std::nullopt;
			}
			state.lastPrunedAt = lastPrunedAt;
		}
		if (!result.emplace(PeerId(peerRaw), std::move(state)).second) {
			return std::nullopt;
		}
	}
	return (reader.status() == QDataStream::Ok && reader.atEnd())
		? std::make_optional(std::move(result))
		: std::nullopt;
}

QByteArray SerializeJournal(const Journal &journal) {
	auto result = QByteArray();
	auto writer = QDataStream(&result, QIODevice::WriteOnly);
	writer.setVersion(QDataStream::Qt_5_1);
	writer << kJournalVersion << quint32(journal.size());
	for (const auto &[id, data] : journal) {
		writer
			<< quint64(id.peer.value)
			<< qint64(id.msg.bare)
			<< data;
	}
	return result;
}

std::optional<Journal> DeserializeJournal(QByteArray data) {
	if (data.isEmpty()) {
		return Journal();
	}
	auto reader = QDataStream(&data, QIODevice::ReadOnly);
	reader.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto count = quint32();
	reader >> version >> count;
	if (reader.status() != QDataStream::Ok
		|| version != kJournalVersion
		|| count > kMaxJournalEntries) {
		return std::nullopt;
	}
	auto result = Journal();
	for (auto index = quint32(); index != count; ++index) {
		auto peerRaw = quint64();
		auto messageRaw = qint64();
		auto entryData = QByteArray();
		reader >> peerRaw >> messageRaw >> entryData;
		const auto id = FullMsgId(PeerId(peerRaw), MsgId(messageRaw));
		const auto entry = DeserializeEntry(entryData);
		if (reader.status() != QDataStream::Ok
			|| !peerRaw
			|| !IsServerMsgId(id.msg)
			|| !entry
			|| entry->id != id
			|| !result.emplace(id, std::move(entryData)).second) {
			return std::nullopt;
		}
	}
	return (reader.status() == QDataStream::Ok && reader.atEnd())
		? std::make_optional(std::move(result))
		: std::nullopt;
}

QByteArray SerializeEntry(const LocalArchiveEntry &entry) {
	auto result = QByteArray();
	auto writer = QDataStream(&result, QIODevice::WriteOnly);
	writer.setVersion(QDataStream::Qt_5_1);
	writer
		<< kEntryVersion
		<< quint64(entry.id.peer.value)
		<< qint64(entry.id.msg.bare)
		<< quint64(entry.senderId.value)
		<< entry.senderName
		<< qint32(entry.date)
		<< quint64(entry.replyTo.peer.value)
		<< qint64(entry.replyTo.msg.bare)
		<< quint8(entry.outgoing ? 1 : 0)
		<< quint8(entry.deleted ? 1 : 0)
		<< qint32(entry.deletedAt)
		<< quint32(entry.versions.size());
	for (const auto &version : entry.versions) {
		writer
			<< qint32(version.observedAt)
			<< version.text
			<< version.media;
	}
	return result;
}

std::optional<LocalArchiveEntry> DeserializeEntry(QByteArray data) {
	if (data.isEmpty()) {
		return std::nullopt;
	}
	auto reader = QDataStream(&data, QIODevice::ReadOnly);
	reader.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto peerRaw = quint64();
	auto messageRaw = qint64();
	auto senderRaw = quint64();
	auto senderName = QString();
	auto date = qint32();
	auto replyPeerRaw = quint64();
	auto replyMessageRaw = qint64();
	auto outgoing = quint8();
	auto deleted = quint8();
	auto deletedAt = qint32();
	auto versionsCount = quint32();
	reader
		>> version
		>> peerRaw
		>> messageRaw
		>> senderRaw
		>> senderName
		>> date
		>> replyPeerRaw
		>> replyMessageRaw
		>> outgoing
		>> deleted
		>> deletedAt
		>> versionsCount;
	if (reader.status() != QDataStream::Ok
		|| version != kEntryVersion
		|| !peerRaw
		|| !IsServerMsgId(MsgId(messageRaw))
		|| date < 0
		|| outgoing > 1
		|| deleted > 1
		|| deletedAt < 0
		|| !versionsCount
		|| versionsCount > kMaxVersionsPerMessage) {
		return std::nullopt;
	}
	auto result = LocalArchiveEntry{
		.id = FullMsgId(PeerId(peerRaw), MsgId(messageRaw)),
		.senderId = PeerId(senderRaw),
		.senderName = std::move(senderName),
		.date = date,
		.replyTo = FullMsgId(
			PeerId(replyPeerRaw),
			MsgId(replyMessageRaw)),
		.outgoing = (outgoing != 0),
		.deleted = (deleted != 0),
		.deletedAt = deletedAt,
	};
	result.versions.reserve(versionsCount);
	for (auto index = quint32(); index != versionsCount; ++index) {
		auto observedAt = qint32();
		auto text = QString();
		auto media = QString();
		reader >> observedAt >> text >> media;
		if (reader.status() != QDataStream::Ok || observedAt < 0) {
			return std::nullopt;
		}
		result.versions.push_back({
			.observedAt = observedAt,
			.text = std::move(text),
			.media = std::move(media),
		});
	}
	return (reader.status() == QDataStream::Ok && reader.atEnd())
		? std::make_optional(std::move(result))
		: std::nullopt;
}

std::vector<quint32> BuildSearchHashes(
		const LocalArchiveEntry &entry) {
	auto result = std::vector<quint32>();
	if (!AddSearchHashes(result, entry.senderName)
		|| !AddSearchHashes(result, QString::number(entry.id.msg.bare))) {
		return {};
	}
	for (const auto &version : entry.versions) {
		if (!AddSearchHashes(result, version.text)
			|| !AddSearchHashes(result, version.media)) {
			return {};
		}
	}
	return FinishSearchHashes(std::move(result));
}

std::vector<quint32> BuildSenderHashes(const QString &sender) {
	auto result = std::vector<quint32>();
	return AddSearchHashes(result, sender)
		? FinishSearchHashes(std::move(result))
		: std::vector<quint32>();
}

std::vector<quint32> BuildQueryHashes(const QStringList &terms) {
	auto result = std::vector<quint32>();
	for (const auto &term : terms) {
		if (term.size() < 3 || !AddSearchHashes(result, term)) {
			return {};
		}
	}
	return FinishSearchHashes(std::move(result));
}

bool SearchMayMatch(
		const std::vector<quint32> &stored,
		const std::vector<quint32> &query) {
	return query.empty()
		|| stored.empty()
		|| std::includes(
			begin(stored),
			end(stored),
			begin(query),
			end(query));
}

bool AppendVersion(
		LocalArchiveEntry &entry,
		LocalArchiveVersion version) {
	Expects(!entry.versions.empty());

	const auto &latest = entry.versions.back();
	if (latest.text == version.text && latest.media == version.media) {
		return false;
	}
	if (entry.versions.size() >= kMaxVersionsPerMessage) {
		entry.versions.erase(entry.versions.begin() + 1);
	}
	entry.versions.push_back(std::move(version));
	return true;
}

bool MarkDeleted(LocalArchiveEntry &entry, TimeId eventAt) {
	if (entry.deleted) {
		return false;
	}
	entry.deleted = true;
	entry.deletedAt = eventAt;
	return true;
}

TimeId RetentionSeconds(LocalArchiveRetention retention) {
	switch (retention) {
	case LocalArchiveRetention::Week: return 7 * 24 * 60 * 60;
	case LocalArchiveRetention::Month: return 30 * 24 * 60 * 60;
	case LocalArchiveRetention::Year: return 365 * 24 * 60 * 60;
	case LocalArchiveRetention::Off:
	case LocalArchiveRetention::Forever: return 0;
	}
	Unexpected("LocalArchiveRetention value.");
}

bool Expired(
		TimeId date,
		LocalArchiveRetention retention,
		TimeId now) {
	const auto seconds = RetentionSeconds(retention);
	return seconds && date > 0 && date < (now - seconds);
}

} // namespace Data::LocalArchiveCodec
