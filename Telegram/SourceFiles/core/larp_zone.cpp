/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/larp_zone.h"

#include "base/options.h"
#include "base/unixtime.h"

namespace LarpZone {
namespace {

base::options::option<QString> ZoneOption({
	.id = "larp-zone",
	.name = "LarpZone",
	.description = "Time zone for chat timestamps and Telegram connections.",
	.restartRequired = true,
});

QTimeZone ActiveZone;

} // namespace

void Start() {
	const auto id = ZoneOption.value().toUtf8();
	ActiveZone = id.isEmpty() ? QTimeZone() : QTimeZone(id);
}

bool UsesDeviceZone() {
	return !ActiveZone.isValid();
}

QTimeZone TimeZone() {
	return UsesDeviceZone() ? QTimeZone::systemTimeZone() : ActiveZone;
}

QDateTime Now() {
	const auto local = QDateTime::currentDateTime();
	return UsesDeviceZone() ? local : local.toTimeZone(ActiveZone);
}

QDateTime ChatTime(TimeId date) {
	const auto local = base::unixtime::parse(date);
	return (UsesDeviceZone() || !local.isValid())
		? local
		: local.toTimeZone(ActiveZone);
}

QByteArray SavedId() {
	const auto id = ZoneOption.value().toUtf8();
	return QTimeZone(id).isValid() ? id : QByteArray();
}

rpl::producer<QByteArray> SavedIdValue() {
	return rpl::single(SavedId()) | rpl::then(
		ZoneOption.changes() | rpl::map([] { return SavedId(); }));
}

void Save(const QByteArray &id) {
	ZoneOption.set(QString::fromLatin1(id));
}

} // namespace LarpZone
