/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_own_group_messages.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kPageSize = 100;

} // namespace

struct OwnGroupMessages::Operation {
	not_null<PeerData*> group;
	not_null<PeerData*> peer;
	Fn<void(Result)> done;
	TimeId until = 0;
	MsgId before;
	std::vector<QVector<MTPint>> batches;
	bool deleted = false;
	bool skipped = false;
};

OwnGroupMessages::OwnGroupMessages(not_null<ApiWrap*> api) : _api(api) {
}

bool OwnGroupMessages::CanDelete(not_null<PeerData*> group) {
	if (const auto chat = group->asChat()) {
		return chat->amIn();
	} else if (const auto channel = group->asMegagroup()) {
		return channel->amIn() && !channel->isMonoforum();
	}
	return false;
}

bool OwnGroupMessages::busy(not_null<PeerData*> group) const {
	return _active.contains(group->id);
}

void OwnGroupMessages::clear(not_null<PeerData*> group, Fn<void(Result)> done) {
	if (&group->session() != &_api->session() || !CanDelete(group)) {
		done(Result::Failed);
		return;
	} else if (!_active.emplace(group->id).second) {
		done(Result::Busy);
		return;
	}
	search(std::make_shared<Operation>(Operation{
		.group = group,
		.peer = group,
		.done = std::move(done),
		.until = base::unixtime::now(),
	}));
}

void OwnGroupMessages::search(const Job &job) {
	_api->request(MTPmessages_Search(
		MTP_flags(MTPmessages_Search::Flag::f_from_id),
		job->peer->input(),
		MTP_string(),
		MTP_inputPeerSelf(),
		MTPInputPeer(), // saved_peer_id
		MTPVector<MTPReaction>(),
		MTPint(), // top_msg_id
		MTP_inputMessagesFilterEmpty(),
		MTPint(), // min_date
		MTP_int(job->until),
		MTPint(), // offset_id
		MTPint(), // add_offset
		MTP_int(kPageSize),
		MTP_int(job->before), // max_id, exclusive even after deletion
		MTPint(), // min_id
		MTPlong() // hash
	)).done([=](const MTPmessages_Messages &result) {
		received(job, result);
	}).fail([=](const MTP::Error &error) {
		finish(job, MTP::IgnoreError(error) ? Result::Silent : Result::Failed);
	}).send();
}

void OwnGroupMessages::received(
		const Job &job,
		const MTPmessages_Messages &result) {
	const auto messages = result.match([](
			const MTPDmessages_messagesNotModified &) -> const QVector<MTPMessage>* {
		return nullptr;
	}, [](const auto &data) {
		return &data.vmessages().v;
	});
	if (!messages) {
		finish(job, Result::Failed);
		return;
	} else if (messages->empty()) {
		if (const auto migrated = job->peer->migrateFrom()) {
			job->peer = migrated;
			job->before = 0;
			search(job);
		} else {
			finish(job, job->skipped
				? Result::Partial
				: job->deleted ? Result::Deleted : Result::Empty);
		}
		return;
	}
	const auto peer = job->peer;
	auto &owner = peer->owner();
	owner.processExistingMessages(peer->asChannel(), result);
	const auto self = _api->session().userPeerId();
	const auto now = base::unixtime::now();
	auto next = MsgId();
	auto ids = QVector<MTPint>();
	for (const auto &message : *messages) {
		const auto id = IdFromMessage(message);
		const auto messagePeer = PeerFromMessage(message);
		if ((messagePeer && messagePeer != peer->id)
			|| !IsServerMsgId(id)
			|| (job->before && id >= job->before)) {
			continue;
		}
		if (!next || id < next) {
			next = id;
		}
		if (message.type() != mtpc_message || messagePeer != peer->id) {
			continue;
		}
		const auto &data = message.c_message();
		const auto from = data.vfrom_id();
		if (!data.is_out() || !from || peerFromMTP(*from) != self
			|| data.vdate().v > job->until) {
			continue;
		}
		const auto item = owner.message(peer->id, id);
		if (item && item->out() && item->from()->id == self
			&& !item->isService() && item->canDelete()
			&& (peer->isChannel() || item->canDeleteForEveryone(now))) {
			if (!ids.contains(MTP_int(id))) {
				ids.push_back(MTP_int(id));
			}
		} else {
			job->skipped = true;
		}
	}
	if (!next) {
		finish(job, Result::Failed);
		return;
	}
	job->before = next;
	if (!ids.empty()) {
		job->batches.push_back(std::move(ids));
	}
	deleteNext(job);
}

void OwnGroupMessages::deleteNext(const Job &job) {
	if (job->batches.empty()) {
		search(job);
		return;
	}
	const auto ids = std::move(job->batches.back());
	job->batches.pop_back();
	const auto peer = job->peer;
	peer->owner().histories().deleteMessages(
		peer->owner().history(peer),
		ids,
		true,
		[=](const MTP::Error *error) {
			if (!error) {
				peer->owner().processMessagesDeleted(peer->id, ids);
				peer->owner().sendHistoryChangeNotifications();
				job->deleted = true;
			} else if (error->type() == u"MESSAGE_DELETE_FORBIDDEN"_q
				|| error->type() == u"MESSAGE_ID_INVALID"_q
				|| error->type() == u"MSG_ID_INVALID"_q) {
				if (ids.size() == 1) {
					job->skipped = true;
				} else {
					const auto half = ids.size() / 2;
					job->batches.push_back(ids.mid(half));
					job->batches.push_back(ids.mid(0, half));
				}
			} else {
				finish(job, MTP::IgnoreError(*error) ? Result::Silent : Result::Failed);
				return;
			}
			deleteNext(job);
		});
}

void OwnGroupMessages::finish(const Job &job, Result result) {
	_active.remove(job->group->id);
	job->done(result);
}

} // namespace Api
