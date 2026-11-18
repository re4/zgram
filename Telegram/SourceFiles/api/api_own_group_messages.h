/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "data/data_peer_id.h"

class ApiWrap;
class PeerData;

namespace Api {

class OwnGroupMessages final {
public:
	enum class Result { Deleted, Empty, Partial, Failed, Busy, Silent };

	explicit OwnGroupMessages(not_null<ApiWrap*> api);
	[[nodiscard]] static bool CanDelete(not_null<PeerData*> group);
	[[nodiscard]] bool busy(not_null<PeerData*> group) const;
	void clear(not_null<PeerData*> group, Fn<void(Result)> done);

private:
	struct Operation;
	using Job = std::shared_ptr<Operation>;

	void search(const Job &job);
	void received(const Job &job, const MTPmessages_Messages &result);
	void deleteNext(const Job &job);
	void finish(const Job &job, Result result);

	const not_null<ApiWrap*> _api;
	base::flat_set<PeerId> _active;

};

} // namespace Api
