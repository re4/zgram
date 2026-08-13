/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_local_bookmarks_codec.h"

#include "base/flat_set.h"

#include <QtCore/QDataStream>

namespace Data::LocalBookmarksCodec {
namespace {

constexpr auto kVersion = qint32(1);
constexpr auto kMaxEntries = quint32(1000);

} // namespace

QByteArray Serialize(const std::vector<LocalBookmark> &entries) {
	auto result = QByteArray();
	auto writer = QDataStream(&result, QIODevice::WriteOnly);
	writer.setVersion(QDataStream::Qt_5_1);
	writer << kVersion << quint32(entries.size());
	for (const auto &entry : entries) {
		writer
			<< quint64(entry.id.peer.value)
			<< qint64(entry.id.msg.bare)
			<< quint64(entry.senderId.value)
			<< entry.peerName
			<< entry.senderName
			<< qint32(entry.date)
			<< qint32(entry.savedAt)
			<< quint64(entry.replyTo.peer.value)
			<< qint64(entry.replyTo.msg.bare)
			<< quint8(entry.outgoing ? 1 : 0)
			<< quint8(entry.deleted ? 1 : 0)
			<< qint32(entry.deletedAt)
			<< entry.text
			<< entry.media;
	}
	return result;
}

std::optional<std::vector<LocalBookmark>> Deserialize(QByteArray data) {
	if (data.isEmpty()) {
		return std::vector<LocalBookmark>();
	}
	auto reader = QDataStream(&data, QIODevice::ReadOnly);
	reader.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto count = quint32();
	reader >> version >> count;
	if (reader.status() != QDataStream::Ok
		|| version != kVersion
		|| count > kMaxEntries) {
		return std::nullopt;
	}
	auto result = std::vector<LocalBookmark>();
	auto ids = base::flat_set<FullMsgId>();
	result.reserve(count);
	for (auto index = quint32(); index != count; ++index) {
		auto peerRaw = quint64();
		auto messageRaw = qint64();
		auto senderRaw = quint64();
		auto peerName = QString();
		auto senderName = QString();
		auto date = qint32();
		auto savedAt = qint32();
		auto replyPeerRaw = quint64();
		auto replyMessageRaw = qint64();
		auto outgoing = quint8();
		auto deleted = quint8();
		auto deletedAt = qint32();
		auto text = QString();
		auto media = QString();
		reader
			>> peerRaw
			>> messageRaw
			>> senderRaw
			>> peerName
			>> senderName
			>> date
			>> savedAt
			>> replyPeerRaw
			>> replyMessageRaw
			>> outgoing
			>> deleted
			>> deletedAt
			>> text
			>> media;
		const auto id = FullMsgId(PeerId(peerRaw), MsgId(messageRaw));
		if (reader.status() != QDataStream::Ok
			|| !peerRaw
			|| !IsServerMsgId(id.msg)
			|| date < 0
			|| savedAt < 0
			|| outgoing > 1
			|| deleted > 1
			|| deletedAt < 0
			|| !ids.emplace(id).second) {
			return std::nullopt;
		}
		result.push_back({
			.id = id,
			.senderId = PeerId(senderRaw),
			.peerName = std::move(peerName),
			.senderName = std::move(senderName),
			.date = date,
			.savedAt = savedAt,
			.replyTo = FullMsgId(
				PeerId(replyPeerRaw),
				MsgId(replyMessageRaw)),
			.outgoing = (outgoing != 0),
			.deleted = (deleted != 0),
			.deletedAt = deletedAt,
			.text = std::move(text),
			.media = std::move(media),
		});
	}
	return (reader.status() == QDataStream::Ok && reader.atEnd())
		? std::make_optional(std::move(result))
		: std::nullopt;
}

} // namespace Data::LocalBookmarksCodec
