/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_local_archive.h"

#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "data/data_local_archive.h"
#include "data/data_local_bookmarks.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_advanced.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "settings/settings_modern_ui.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QSaveFile>
#include <QtCore/QStringList>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace Settings {
namespace {

using namespace Builder;

constexpr auto kMaxVisibleEntries = 500;
constexpr auto kMaxSearchCandidates = 2000;

[[nodiscard]] rpl::producer<QString> RetentionTextValue(
		Data::LocalArchiveRetention retention) {
	switch (retention) {
	case Data::LocalArchiveRetention::Off:
		return tr::lng_local_archive_retention_off();
	case Data::LocalArchiveRetention::Week:
		return tr::lng_local_archive_retention_week();
	case Data::LocalArchiveRetention::Month:
		return tr::lng_local_archive_retention_month();
	case Data::LocalArchiveRetention::Year:
		return tr::lng_local_archive_retention_year();
	case Data::LocalArchiveRetention::Forever:
		return tr::lng_local_archive_retention_forever();
	}
	Unexpected("LocalArchiveRetention value.");
}

[[nodiscard]] std::vector<QString> RetentionOptions() {
	return {
		tr::lng_local_archive_retention_off(tr::now),
		tr::lng_local_archive_retention_week(tr::now),
		tr::lng_local_archive_retention_month(tr::now),
		tr::lng_local_archive_retention_year(tr::now),
		tr::lng_local_archive_retention_forever(tr::now),
	};
}

[[nodiscard]] Data::LocalArchiveRetention RetentionFromIndex(int index) {
	switch (index) {
	case 0: return Data::LocalArchiveRetention::Off;
	case 1: return Data::LocalArchiveRetention::Week;
	case 2: return Data::LocalArchiveRetention::Month;
	case 3: return Data::LocalArchiveRetention::Year;
	case 4: return Data::LocalArchiveRetention::Forever;
	}
	Unexpected("Local archive retention index.");
}

[[nodiscard]] int RetentionIndex(Data::LocalArchiveRetention retention) {
	return int(retention);
}

[[nodiscard]] rpl::producer<QString> PeerSummary(
		not_null<Data::LocalArchive*> archive,
		PeerId peerId) {
	return rpl::single(rpl::empty) | rpl::then(
		archive->changes()
	) | rpl::map([=] {
		const auto peers = archive->peers();
		const auto i = ranges::find(peers, peerId, &Data::LocalArchivePeer::id);
		if (i == end(peers)) {
			return rpl::single(QString());
		}
		return tr::lng_local_archive_peer_summary(
			lt_count,
			rpl::single(float64(i->messages)) | tr::to_count(),
			lt_size,
			rpl::single(Ui::FormatSizeText(i->bytes)),
			lt_retention,
			RetentionTextValue(i->retention));
	}) | rpl::flatten_latest();
}

[[nodiscard]] QString MessageDate(TimeId date) {
	return QLocale().toString(
		QDateTime::fromSecsSinceEpoch(date),
		QLocale::ShortFormat);
}

[[nodiscard]] QString VersionText(
		const Data::LocalArchiveVersion &version) {
	auto result = version.text;
	if (!version.media.isEmpty()) {
		if (!result.isEmpty()) {
			result += '\n';
		}
		result += tr::lng_local_archive_media(tr::now)
			+ u": "_q
			+ version.media;
	}
	return result;
}

[[nodiscard]] QString EntryMetaText(
		const Data::LocalArchiveEntry &entry) {
	return entry.senderName
		+ u"  ·  "_q
		+ MessageDate(entry.date)
		+ u"  ·  #"_q
		+ QString::number(entry.id.msg.bare);
}

[[nodiscard]] bool EntryHasMedia(
		const Data::LocalArchiveEntry &entry) {
	return ranges::any_of(entry.versions, [](const auto &version) {
		return !version.media.isEmpty();
	});
}

[[nodiscard]] QString EntryBadgeText(
		const Data::LocalArchiveEntry &entry) {
	if (entry.deleted) {
		return tr::lng_local_archive_deleted_badge(tr::now);
	} else if (entry.versions.size() > 1) {
		return tr::lng_local_archive_edited_badge(tr::now);
	} else if (EntryHasMedia(entry)) {
		return tr::lng_local_archive_media_badge(tr::now);
	}
	return tr::lng_local_archive_message_badge(tr::now);
}

[[nodiscard]] ModernUi::Tone EntryTone(
		const Data::LocalArchiveEntry &entry) {
	return entry.deleted
		? ModernUi::Tone::Attention
		: (entry.versions.size() > 1 || EntryHasMedia(entry))
		? ModernUi::Tone::Accent
		: ModernUi::Tone::Neutral;
}

[[nodiscard]] QString EntryBodyText(
		const Data::LocalArchiveEntry &entry) {
	auto lines = QStringList();
	if (entry.replyTo.msg) {
		lines.push_back(tr::lng_local_archive_reply_to(
			tr::now,
			lt_message_id,
			QString::number(entry.replyTo.msg.bare)));
	}
	if (entry.versions.size() == 1) {
		lines.push_back(VersionText(entry.versions.front()));
	} else {
		for (auto i = 0, count = int(entry.versions.size());
				i != count;
				++i) {
			const auto title = (i == 0)
				? tr::lng_local_archive_before(tr::now)
				: (i + 1 == count)
				? tr::lng_local_archive_after(tr::now)
				: tr::lng_local_archive_version(
					tr::now,
					lt_index,
					QString::number(i + 1));
			lines.push_back(
				title
				+ u" — "_q
				+ MessageDate(entry.versions[i].observedAt)
				+ u"\n"_q
				+ VersionText(entry.versions[i]));
		}
	}
	return lines.join(u"\n"_q);
}

[[nodiscard]] bool MatchesQuery(
		const Data::LocalArchiveEntry &entry,
		const QString &query) {
	const auto tokens = query.simplified().split(' ', Qt::SkipEmptyParts);
	if (tokens.empty()) {
		return true;
	}
	auto searchable = entry.senderName;
	for (const auto &version : entry.versions) {
		searchable += u" "_q + version.text + u" "_q + version.media;
	}
	searchable += u" "_q + QString::number(entry.id.msg.bare);
	for (const auto &token : tokens) {
		if (token.compare(u"is:message"_q, Qt::CaseInsensitive) == 0) {
			continue;
		} else if (token.compare(u"is:deleted"_q, Qt::CaseInsensitive) == 0) {
			if (!entry.deleted) {
				return false;
			}
		} else if (token.compare(u"is:edited"_q, Qt::CaseInsensitive) == 0
			|| token.compare(u"has:edits"_q, Qt::CaseInsensitive) == 0) {
			if (entry.versions.size() < 2) {
				return false;
			}
		} else if (token.compare(u"has:media"_q, Qt::CaseInsensitive) == 0) {
			if (ranges::none_of(entry.versions, [](const auto &version) {
				return !version.media.isEmpty();
			})) {
				return false;
			}
		} else if (token.startsWith(u"from:"_q, Qt::CaseInsensitive)) {
			if (!entry.senderName.contains(
					token.mid(5),
					Qt::CaseInsensitive)) {
				return false;
			}
		} else if (!searchable.contains(token, Qt::CaseInsensitive)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] Data::LocalArchiveSearchQuery ParseSearchQuery(
		const QString &query) {
	auto result = Data::LocalArchiveSearchQuery{
		.limit = kMaxSearchCandidates,
	};
	const auto tokens = query.simplified().split(' ', Qt::SkipEmptyParts);
	for (const auto &token : tokens) {
		if (token.compare(u"is:message"_q, Qt::CaseInsensitive) == 0) {
			continue;
		} else if (token.compare(
				u"is:deleted"_q,
				Qt::CaseInsensitive) == 0) {
			result.deletedOnly = true;
		} else if (token.compare(u"is:edited"_q, Qt::CaseInsensitive) == 0
			|| token.compare(u"has:edits"_q, Qt::CaseInsensitive) == 0) {
			result.editedOnly = true;
		} else if (token.compare(
				u"has:media"_q,
				Qt::CaseInsensitive) == 0) {
			result.mediaOnly = true;
		} else if (token.startsWith(u"from:"_q, Qt::CaseInsensitive)) {
			result.sender = token.mid(5);
		} else {
			result.terms.push_back(token);
		}
	}
	return result;
}

[[nodiscard]] QByteArray JsonExport(
		PeerId peerId,
		const QString &peerName,
		const std::vector<Data::LocalArchiveEntry> &entries) {
	auto messages = QJsonArray();
	for (const auto &entry : entries) {
		auto versions = QJsonArray();
		for (const auto &version : entry.versions) {
			versions.push_back(QJsonObject{
				{ u"observed_at"_q, qint64(version.observedAt) },
				{ u"text"_q, version.text },
				{ u"media"_q, version.media },
			});
		}
		messages.push_back(QJsonObject{
			{ u"message_id"_q, QString::number(entry.id.msg.bare) },
			{ u"sender_id"_q, QString::number(entry.senderId.value) },
			{ u"sender_name"_q, entry.senderName },
			{ u"timestamp"_q, qint64(entry.date) },
			{ u"reply_peer_id"_q, QString::number(entry.replyTo.peer.value) },
			{ u"reply_message_id"_q, QString::number(entry.replyTo.msg.bare) },
			{ u"outgoing"_q, entry.outgoing },
			{ u"deleted_on_telegram"_q, entry.deleted },
			{ u"deleted_at"_q, qint64(entry.deletedAt) },
			{ u"versions"_q, versions },
		});
	}
	return QJsonDocument(QJsonObject{
		{ u"format_version"_q, 1 },
		{ u"peer_id"_q, QString::number(peerId.value) },
		{ u"peer_name"_q, peerName },
		{ u"exported_at"_q, QDateTime::currentDateTimeUtc().toString(
			Qt::ISODate) },
		{ u"messages"_q, messages },
	}).toJson(QJsonDocument::Indented);
}

[[nodiscard]] QByteArray HtmlExport(
		const QString &peerName,
		const std::vector<Data::LocalArchiveEntry> &entries) {
	auto result = u"<!doctype html><html><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>"_q
		+ peerName.toHtmlEscaped()
		+ u" — "_q
		+ tr::lng_local_archive_title(tr::now).toHtmlEscaped()
		+ u"</title><style>"
		"body{font:15px system-ui,sans-serif;max-width:900px;margin:40px auto;"
		"padding:0 20px;color:#202124;background:#f6f7f8}"
		"article{background:white;border-radius:12px;padding:16px;margin:12px 0;"
		"box-shadow:0 1px 3px #0002}header{color:#667085;margin-bottom:10px}"
		".deleted{border-left:4px solid #d94b4b}.status{color:#b42318}"
		".version{border-top:1px solid #e5e7eb;margin-top:10px;padding-top:10px}"
		"pre{font:inherit;white-space:pre-wrap;margin:6px 0}</style></head><body><h1>"_q
		+ peerName.toHtmlEscaped()
		+ u"</h1><p>"_q
		+ tr::lng_local_archive_about(tr::now).toHtmlEscaped()
		+ u"</p>"_q;
	for (const auto &entry : entries) {
		result += entry.deleted
			? u"<article class=\"deleted\">"_q
			: u"<article>"_q;
		result += u"<header>"_q
			+ MessageDate(entry.date).toHtmlEscaped()
			+ u" · "_q
			+ entry.senderName.toHtmlEscaped()
			+ u" · #"_q
			+ QString::number(entry.id.msg.bare)
			+ u"</header>"_q;
		if (entry.replyTo.msg) {
			result += u"<p>"_q
				+ tr::lng_local_archive_reply_to(
					tr::now,
					lt_message_id,
					QString::number(entry.replyTo.msg.bare)).toHtmlEscaped()
				+ u"</p>"_q;
		}
		if (entry.deleted) {
			result += u"<p class=\"status\">"_q
				+ tr::lng_local_archive_deleted_status(tr::now).toHtmlEscaped()
				+ (entry.deletedAt
					? u" \u00B7 "_q
						+ MessageDate(entry.deletedAt).toHtmlEscaped()
					: QString())
				+ u"</p>"_q;
		}
		for (auto i = 0, count = int(entry.versions.size());
				i != count;
				++i) {
			const auto title = (entry.versions.size() == 1)
				? QString()
				: (i == 0)
				? tr::lng_local_archive_before(tr::now)
				: (i + 1 == count)
				? tr::lng_local_archive_after(tr::now)
				: tr::lng_local_archive_version(
					tr::now,
					lt_index,
					QString::number(i + 1));
			result += u"<section class=\"version\">"_q;
			if (!title.isEmpty()) {
				result += u"<strong>"_q
					+ title.toHtmlEscaped()
					+ u"</strong> · "_q
					+ MessageDate(entry.versions[i].observedAt).toHtmlEscaped();
			}
			result += u"<pre>"_q
				+ VersionText(entry.versions[i]).toHtmlEscaped()
				+ u"</pre></section>"_q;
		}
		result += u"</article>"_q;
	}
	result += u"</body></html>"_q;
	return result.toUtf8();
}

[[nodiscard]] bool WriteExport(const QString &path, const QByteArray &data) {
	auto file = QSaveFile(path);
	return file.open(QIODevice::WriteOnly)
		&& file.write(data) == data.size()
		&& file.commit();
}

void ExportArchive(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		PeerId peerId,
		QString peerName,
		std::shared_ptr<const std::vector<Data::LocalArchiveEntry>> entries,
		bool html) {
	const auto caption = html
		? tr::lng_local_archive_export_html(tr::now)
		: tr::lng_local_archive_export_json(tr::now);
	const auto filter = html
		? u"HTML (*.html);;"_q + FileDialog::AllFilesFilter()
		: u"JSON (*.json);;"_q + FileDialog::AllFilesFilter();
	const auto initial = html
		? u"local-archive.html"_q
		: u"local-archive.json"_q;
	FileDialog::GetWritePath(
		box.get(),
		caption,
		filter,
		initial,
		crl::guard(box, [=](const QString &path) {
			if (path.isEmpty()) {
				return;
			}
			const auto data = html
				? HtmlExport(peerName, *entries)
				: JsonExport(peerId, peerName, *entries);
			controller->showToast(WriteExport(path, data)
				? tr::lng_local_archive_export_done(tr::now)
				: tr::lng_local_archive_export_failed(tr::now));
		}));
}

void AddTimelineEntry(
		not_null<Ui::VerticalLayout*> container,
		const Data::LocalArchiveEntry &entry) {
	const auto tone = EntryTone(entry);
	const auto panel = container->add(
		object_ptr<ModernUi::Panel>(container, tone),
		st::zgramBoxCardMargin);
	const auto body = panel->body();
	body->add(
		object_ptr<ModernUi::Badge>(
			body,
			rpl::single(EntryBadgeText(entry)),
			tone),
		st::zgramTimelineBadgeMargin);
	const auto meta = body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			rpl::single(EntryMetaText(entry)),
			st::zgramTimelineMeta),
		st::zgramTimelineMetaMargin);
	meta->setSelectable(true);
	if (entry.deleted) {
		const auto statusText = tr::lng_local_archive_deleted_status(tr::now)
			+ (entry.deletedAt
				? u" · "_q + MessageDate(entry.deletedAt)
				: QString());
		const auto status = body->add(
			object_ptr<Ui::FlatLabel>(
				body,
				rpl::single(statusText),
				st::zgramTimelineAttention),
			st::zgramTimelineStatusMargin);
		status->setSelectable(true);
	}
	const auto content = body->add(
		object_ptr<Ui::FlatLabel>(
			body,
			rpl::single(EntryBodyText(entry)),
			st::zgramTimelineBody),
		st::zgramTimelineBodyMargin);
	content->setSelectable(true);
}

void TimelineBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		PeerId peerId) {
	struct State {
		std::vector<Data::LocalArchiveEntry> entries;
		bool loaded = false;
		int loadRequest = 0;
		int matched = 0;
	};
	const auto session = &controller->session();
	const auto archive = &session->data().localArchive();
	const auto peer = session->data().peer(peerId);
	const auto state = box->lifetime().make_state<State>();

	box->setTitle(tr::lng_local_archive_chat_title(
		lt_name,
		rpl::single(peer->name())));
	box->setWidth(st::boxWideWidth);

	const auto content = box->verticalLayout();
	content->add(ModernUi::MakeHero(content, {
		.title = tr::lng_local_archive_title(),
		.description = tr::lng_local_archive_about(),
		.badge = tr::lng_local_archive_device_badge(),
		.icon = &st::menuIconGroupLog,
	}), st::zgramBoxHeroMargin);
	const auto search = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::zgramSearchField,
			tr::lng_local_archive_search()),
		st::zgramSearchMargin);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_local_archive_search_hint(),
			st::zgramHint),
		st::zgramHintMargin);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_local_archive_timeline_section(),
			st::zgramSectionTitle),
		st::zgramBoxSectionMargin);
	const auto results = content->add(object_ptr<Ui::VerticalLayout>(content));

	const auto refresh = [=] {
		results->clear();
		if (!state->loaded) {
			results->add(ModernUi::MakeEmptyState(
				results,
				tr::lng_local_archive_loading(),
				ModernUi::Tone::Accent),
				st::zgramBoxCardMargin);
			return;
		}
		const auto query = search->getTextWithTags().text;
		auto shown = 0;
		auto matched = 0;
		for (const auto &entry : state->entries) {
			if (!MatchesQuery(entry, query)) {
				continue;
			}
			++matched;
			if (shown >= kMaxVisibleEntries) {
				continue;
			}
			++shown;
			AddTimelineEntry(results, entry);
		}
		if (!matched) {
			results->add(ModernUi::MakeEmptyState(
				results,
				tr::lng_local_archive_empty()),
				st::zgramBoxCardMargin);
		} else if (matched > shown
			|| state->matched > int(state->entries.size())) {
			results->add(ModernUi::MakeEmptyState(
				results,
				tr::lng_local_archive_more_results(),
				ModernUi::Tone::Accent),
				st::zgramBoxCardMargin);
		}
	};
	refresh();
	const auto reload = [=] {
		const auto request = ++state->loadRequest;
		state->loaded = false;
		refresh();
		archive->search(
			peerId,
			ParseSearchQuery(search->getTextWithTags().text),
			crl::guard(box, [=](Data::LocalArchiveSearchResult &&result) {
				if (request != state->loadRequest) {
					return;
				}
				state->entries = std::move(result.entries);
				state->matched = result.matched;
				state->loaded = true;
				refresh();
			}));
	};
	reload();
	archive->changes(
	) | rpl::on_next(reload, box->lifetime());
	search->changes(
	) | rpl::on_next(reload, search->lifetime());
	box->setFocusCallback([=] { search->setFocusFast(); });

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_local_archive_controls_section(),
			st::zgramSectionTitle),
		st::zgramBoxSectionMargin);
	auto retentionValue = rpl::single(rpl::empty)
		| rpl::then(archive->changes())
		| rpl::map([=] {
			return RetentionTextValue(archive->retention(peerId));
		})
		| rpl::flatten_latest();
	content->add(ModernUi::MakeActionCard(content, {
		.title = tr::lng_local_archive_retention(),
		.description = tr::lng_local_archive_retention_about(),
		.badge = std::move(retentionValue),
		.icon = &st::menuIconTimer,
		.clicked = [=] {
			ShowLocalArchiveRetention(controller, peer);
		},
	}), st::zgramBoxCardMargin);

	const auto exportEntries = std::make_shared<
		std::vector<Data::LocalArchiveEntry>>();
	const auto exportArchive = [=](bool html) {
		archive->load(peerId, crl::guard(box, [=](
				std::vector<Data::LocalArchiveEntry> &&entries) {
			*exportEntries = std::move(entries);
			ExportArchive(
				box,
				controller,
				peerId,
				peer->name(),
				exportEntries,
				html);
		}));
	};
	content->add(ModernUi::MakeActionCard(content, {
		.title = tr::lng_local_archive_export_json(),
		.description = tr::lng_local_archive_export_json_about(),
		.icon = &st::menuIconExport,
		.clicked = [=] { exportArchive(false); },
	}), st::zgramBoxCardMargin);
	content->add(ModernUi::MakeActionCard(content, {
		.title = tr::lng_local_archive_export_html(),
		.description = tr::lng_local_archive_export_html_about(),
		.icon = &st::menuIconExport,
		.clicked = [=] { exportArchive(true); },
	}), st::zgramBoxCardMargin);

	const auto weak = base::make_weak(box);
	content->add(ModernUi::MakeActionCard(content, {
		.title = tr::lng_local_archive_clear(),
		.description = tr::lng_local_archive_clear_about(),
		.icon = &st::menuIconDelete,
		.tone = ModernUi::Tone::Attention,
		.clicked = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_local_archive_clear_sure(
					tr::now,
					lt_name,
					tr::bold(peer->name()),
					tr::rich),
				.confirmed = [=] {
					archive->clear(peerId);
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
				},
				.confirmText = tr::lng_local_archive_clear(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		},
	}), st::zgramBoxBottomMargin);
	box->addButton(tr::lng_box_done(), [=] { box->closeBox(); });
}

[[nodiscard]] QString BookmarkDescription(
		const Data::LocalBookmark &entry) {
	auto result = QString();
	if (!entry.senderName.isEmpty() && entry.senderName != entry.peerName) {
		result += entry.senderName + u" · "_q;
	}
	result += MessageDate(entry.date);
	const auto body = !entry.text.isEmpty() ? entry.text : entry.media;
	if (!body.isEmpty()) {
		result += u" · "_q + body.simplified();
	}
	if (entry.deleted) {
		result += u" · "_q + tr::lng_local_bookmarks_deleted(tr::now);
	}
	return result;
}

[[nodiscard]] QString BookmarkSearchText(
		const Data::LocalBookmark &entry) {
	return entry.peerName + u" "_q + BookmarkDescription(entry);
}

void LocalBookmarksBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto bookmarks = &controller->session().data().localBookmarks();
	box->setTitle(tr::lng_local_bookmarks_title());
	box->setWidth(st::boxWideWidth);

	const auto content = box->verticalLayout();
	content->add(ModernUi::MakeHero(content, {
		.title = tr::lng_local_bookmarks_title(),
		.description = tr::lng_local_bookmarks_about(),
		.badge = tr::lng_local_bookmarks_device_badge(),
		.icon = &st::menuIconFave,
	}), st::zgramBoxHeroMargin);
	const auto search = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::zgramSearchField,
			tr::lng_local_bookmarks_search()),
		st::zgramSearchMargin);
	const auto results = content->add(object_ptr<Ui::VerticalLayout>(content));
	const auto refresh = [=] {
		results->clear();
		const auto query = search->getTextWithTags().text.simplified();
		auto shown = 0;
		for (const auto &entry : bookmarks->entries()) {
			const auto text = BookmarkSearchText(entry);
			if (!query.isEmpty()
				&& !text.contains(query, Qt::CaseInsensitive)) {
				continue;
			}
			const auto id = entry.id;
			auto clicked = entry.deleted
				? Fn<void()>([=] { bookmarks->remove(id); })
				: Fn<void()>([=] {
					controller->showPeerHistory(
						id.peer,
						Window::SectionShow::Way::ClearStack,
						id.msg);
				});
			results->add(ModernUi::MakeActionCard(results, {
				.title = rpl::single(entry.peerName),
				.description = rpl::single(BookmarkDescription(entry)),
				.badge = entry.deleted
					? tr::lng_local_archive_deleted_badge()
					: rpl::producer<QString>(),
				.icon = &st::menuIconFave,
				.tone = entry.deleted
					? ModernUi::Tone::Attention
					: ModernUi::Tone::Neutral,
				.clicked = std::move(clicked),
			}), st::zgramBoxCardMargin);
			++shown;
		}
		if (!shown) {
			results->add(ModernUi::MakeEmptyState(
				results,
				tr::lng_local_bookmarks_empty()),
				st::zgramBoxBottomMargin);
		}
	};
	refresh();
	bookmarks->changes(
	) | rpl::on_next(refresh, box->lifetime());
	search->changes(
	) | rpl::on_next(refresh, search->lifetime());
	box->setFocusCallback([=] { search->setFocusFast(); });

	box->addButton(tr::lng_box_done(), [=] { box->closeBox(); });
	if (bookmarks->count()) {
		box->addLeftButton(tr::lng_local_bookmarks_clear(), [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_local_bookmarks_clear_sure(),
				.confirmed = [=] { bookmarks->clear(); },
				.confirmText = tr::lng_local_bookmarks_clear(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		});
	}
}

void BuildLocalArchiveSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto session = builder.session();
	const auto peers = session->data().localArchive().peers();

	ModernUi::AddHero(builder, {
		.title = tr::lng_local_archive_title(),
		.description = tr::lng_local_archive_about(),
		.badge = tr::lng_local_archive_device_badge(),
		.icon = &st::menuIconGroupLog,
	});
	ModernUi::AddSectionTitle(
		builder,
		tr::lng_local_archive_chats_section(),
		true);
	ModernUi::AddAction(builder, {
		.id = u"local_archive/bookmarks"_q,
		.title = tr::lng_local_bookmarks_title(),
		.description = tr::lng_local_bookmarks_card_about(),
		.badge = rpl::single(rpl::empty) | rpl::then(
			session->data().localBookmarks().changes()
		) | rpl::map([=] {
			return QString::number(
				session->data().localBookmarks().count());
		}),
		.icon = { &st::menuIconFave },
		.clicked = [=] {
			if (controller) {
				ShowLocalBookmarks(controller);
			}
		},
		.keywords = { u"bookmarks"_q, u"saved"_q, u"local"_q },
	});
	if (peers.empty()) {
		ModernUi::AddEmptyState(builder, tr::lng_local_archive_none());
	}
	for (const auto &peer : peers) {
		ModernUi::AddAction(builder, {
			.id = u"local_archive/peer/"_q + QString::number(peer.id.value),
			.title = rpl::single(peer.name),
			.description = PeerSummary(
				&session->data().localArchive(),
				peer.id),
			.icon = { &st::menuIconGroupLog },
			.tone = ModernUi::Tone::Accent,
			.clicked = [=] {
				if (controller) {
					ShowLocalArchive(controller, peer.id);
				}
			},
			.keywords = {
				peer.name,
				u"archive"_q,
				u"retention"_q,
			},
		});
	}
	if (!peers.empty()) {
		ModernUi::AddSectionTitle(
			builder,
			tr::lng_local_archive_controls_section());
		ModernUi::AddAction(builder, {
			.id = u"local_archive/delete_all"_q,
			.title = tr::lng_local_archive_delete_all(),
			.description = tr::lng_local_archive_delete_all_about(),
			.icon = { &st::menuIconDeleteAttention },
			.tone = ModernUi::Tone::Attention,
			.clicked = [=] {
				if (!controller) {
					return;
				}
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_local_archive_delete_all_sure(),
					.confirmed = [=] {
						session->data().localArchive().clearAll();
					},
					.confirmText = tr::lng_local_archive_delete_all(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			},
			.keywords = { u"delete"_q, u"clear"_q, u"archive"_q },
		});
	}
}

class ArchiveStorage : public Section<ArchiveStorage> {
public:
	ArchiveStorage(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
	void rebuildContent();
	void checkPeerList();

	not_null<Ui::VerticalLayout*> _content;
	std::vector<std::pair<PeerId, QString>> _shownPeers;

};

const auto kMeta = BuildHelper({
	.id = ArchiveStorage::Id(),
	.parentId = AdvancedId(),
	.title = &tr::lng_local_archive_title,
	.icon = &st::menuIconGroupLog,
}, [](SectionBuilder &builder) {
	BuildLocalArchiveSection(builder);
});

const SectionBuildMethod kArchiveStorageSection = kMeta.build;

ArchiveStorage::ArchiveStorage(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	setupContent();
}

rpl::producer<QString> ArchiveStorage::title() {
	return tr::lng_local_archive_title();
}

void ArchiveStorage::setupContent() {
	rebuildContent();
	Ui::ResizeFitChild(this, _content);
	controller()->session().data().localArchive().changes(
	) | rpl::on_next([=] {
		checkPeerList();
	}, lifetime());
}

void ArchiveStorage::rebuildContent() {
	const auto peers = controller()->session().data().localArchive().peers();
	_shownPeers.clear();
	_shownPeers.reserve(peers.size());
	for (const auto &peer : peers) {
		_shownPeers.emplace_back(peer.id, peer.name);
	}
	_content->clear();
	const auto inner = _content->add(
		object_ptr<Ui::VerticalLayout>(_content),
		st::zgramPagePadding);
	build(inner, kArchiveStorageSection);
}

void ArchiveStorage::checkPeerList() {
	const auto peers = controller()->session().data().localArchive().peers();
	auto shown = std::vector<std::pair<PeerId, QString>>();
	shown.reserve(peers.size());
	for (const auto &peer : peers) {
		shown.emplace_back(peer.id, peer.name);
	}
	if (shown != _shownPeers) {
		rebuildContent();
	}
}

} // namespace

Type LocalArchiveId() {
	return ArchiveStorage::Id();
}

void ShowLocalArchive(
		not_null<Window::SessionController*> controller,
		PeerId peerId) {
	controller->show(Box(TimelineBox, controller, peerId));
}

void ShowLocalArchiveRetention(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	const auto archive = &peer->owner().localArchive();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto options = RetentionOptions();
		SingleChoiceBox(box, {
			.title = tr::lng_local_archive_retention(),
			.options = options,
			.initialSelection = RetentionIndex(
				archive->retention(peer->id)),
			.callback = [=](int index) {
				archive->setRetention(peer, RetentionFromIndex(index));
			},
		});
	}));
}

void ShowLocalBookmarks(
		not_null<Window::SessionController*> controller) {
	controller->show(Box(LocalBookmarksBox, controller));
}

} // namespace Settings
