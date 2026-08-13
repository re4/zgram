/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "data/data_local_archive.h"

#include <QtCore/QByteArray>

#include <optional>

namespace Data::LocalArchiveCodec {

struct StoredInfo {
	TimeId date = 0;
	qint32 size = 0;
	qint32 versions = 0;
	bool deleted = false;
	bool hasMedia = false;
	std::vector<quint32> searchHashes;
	std::vector<quint32> senderHashes;
};

struct PeerState {
	QString name;
	LocalArchiveRetention retention = LocalArchiveRetention::Off;
	base::flat_map<MsgId, StoredInfo> messages;
	uint64 generation = 0;
	TimeId lastPrunedAt = 0;
};

using Peers = base::flat_map<PeerId, PeerState>;
using Journal = base::flat_map<FullMsgId, QByteArray>;

[[nodiscard]] QByteArray SerializeMetadata(const Peers &peers);
[[nodiscard]] std::optional<Peers> DeserializeMetadata(QByteArray data);

[[nodiscard]] QByteArray SerializeJournal(const Journal &journal);
[[nodiscard]] std::optional<Journal> DeserializeJournal(QByteArray data);

[[nodiscard]] QByteArray SerializeEntry(const LocalArchiveEntry &entry);
[[nodiscard]] std::optional<LocalArchiveEntry> DeserializeEntry(
	QByteArray data);

[[nodiscard]] std::vector<quint32> BuildSearchHashes(
	const LocalArchiveEntry &entry);
[[nodiscard]] std::vector<quint32> BuildSenderHashes(
	const QString &sender);
[[nodiscard]] std::vector<quint32> BuildQueryHashes(
	const QStringList &terms);
[[nodiscard]] bool SearchMayMatch(
	const std::vector<quint32> &stored,
	const std::vector<quint32> &query);

bool AppendVersion(
	LocalArchiveEntry &entry,
	LocalArchiveVersion version);
bool MarkDeleted(LocalArchiveEntry &entry, TimeId eventAt);

[[nodiscard]] TimeId RetentionSeconds(LocalArchiveRetention retention);
[[nodiscard]] bool Expired(
	TimeId date,
	LocalArchiveRetention retention,
	TimeId now);

} // namespace Data::LocalArchiveCodec
