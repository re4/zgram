/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_power_user.h"

#include "boxes/background_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/power_user_settings.h"
#include "core/shortcuts.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_local_archive.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "settings/settings_modern_ui.h"
#include "storage/storage_shared_media.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QKeySequence>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Settings {
namespace {

using namespace Builder;

enum class PaletteAction {
	Search,
	SavedMessages,
	Contacts,
	MainSettings,
	PowerUserSettings,
	ChatAppearance,
	Workspaces,
	LocalArchive,
	LocalBookmarks,
	Photos,
	Videos,
	Files,
	Links,
	Music,
	Voice,
};

struct PaletteCommand {
	QString title;
	QString keywords;
	PaletteAction action = PaletteAction::Search;
	PeerData *peer = nullptr;
};

[[nodiscard]] QString LayoutText(PowerUser::LayoutMode mode) {
	return (mode == PowerUser::LayoutMode::Compact)
		? tr::lng_power_user_layout_compact(tr::now)
		: tr::lng_power_user_layout_comfortable(tr::now);
}

[[nodiscard]] QString TransparencyText(
		PowerUser::TransparencyMode mode) {
	switch (mode) {
	case PowerUser::TransparencyMode::Off:
		return tr::lng_power_user_transparency_off(tr::now);
	case PowerUser::TransparencyMode::Subtle:
		return tr::lng_power_user_transparency_subtle(tr::now);
	case PowerUser::TransparencyMode::Glass:
		return tr::lng_power_user_transparency_glass(tr::now);
	}
	Unexpected("TransparencyMode value.");
}

[[nodiscard]] int SidebarWidthIndex(float64 ratio) {
	constexpr auto kValues = std::array{ .28, 5. / 14., .42 };
	auto result = 0;
	for (auto index = 1; index != int(kValues.size()); ++index) {
		if (std::abs(ratio - kValues[index])
			< std::abs(ratio - kValues[result])) {
			result = index;
		}
	}
	return result;
}

[[nodiscard]] QString SidebarWidthText(int index) {
	switch (index) {
	case 0: return tr::lng_power_user_sidebar_narrow(tr::now);
	case 1: return tr::lng_power_user_sidebar_balanced(tr::now);
	case 2: return tr::lng_power_user_sidebar_wide(tr::now);
	}
	Unexpected("Sidebar width index.");
}

[[nodiscard]] QString CommandPaletteShortcutText() {
	return QKeySequence(
		u"Ctrl+K"_q,
		QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

[[nodiscard]] QString PaletteGroupText(PaletteAction action) {
	switch (action) {
	case PaletteAction::Search:
	case PaletteAction::SavedMessages:
	case PaletteAction::Contacts:
		return tr::lng_power_user_command_navigation(tr::now);
	case PaletteAction::MainSettings:
	case PaletteAction::PowerUserSettings:
	case PaletteAction::Workspaces:
		return tr::lng_power_user_command_interface(tr::now);
	case PaletteAction::LocalArchive:
	case PaletteAction::LocalBookmarks:
		return tr::lng_power_user_command_local(tr::now);
	case PaletteAction::ChatAppearance:
	case PaletteAction::Photos:
	case PaletteAction::Videos:
	case PaletteAction::Files:
	case PaletteAction::Links:
	case PaletteAction::Music:
	case PaletteAction::Voice:
		return tr::lng_power_user_command_current_chat(tr::now);
	}
	Unexpected("PaletteAction value.");
}

[[nodiscard]] const style::icon *PaletteIcon(PaletteAction action) {
	switch (action) {
	case PaletteAction::Search: return &st::menuIconSearch;
	case PaletteAction::SavedMessages: return &st::menuIconSavedMessages;
	case PaletteAction::Contacts: return &st::menuIconGroups;
	case PaletteAction::MainSettings:
	case PaletteAction::PowerUserSettings:
		return &st::menuIconSettings;
	case PaletteAction::Workspaces: return &st::menuIconShowInFolder;
	case PaletteAction::LocalArchive: return &st::menuIconGroupLog;
	case PaletteAction::LocalBookmarks: return &st::menuIconFave;
	case PaletteAction::ChatAppearance: return &st::menuIconChangeColors;
	case PaletteAction::Photos:
	case PaletteAction::Videos:
		return &st::menuIconPhoto;
	case PaletteAction::Files:
	case PaletteAction::Music:
	case PaletteAction::Voice:
		return &st::menuIconFile;
	case PaletteAction::Links: return &st::menuIconLink;
	}
	Unexpected("PaletteAction value.");
}

void SuggestRestart(not_null<Window::SessionController*> controller) {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_settings_need_restart(),
		.confirmed = [] { Core::Restart(); },
		.confirmText = tr::lng_settings_restart_now(),
		.cancelText = tr::lng_settings_restart_later(),
	}));
}

void ShowLayoutBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		SingleChoiceBox(box, {
			.title = tr::lng_power_user_layout(),
			.options = {
				tr::lng_power_user_layout_comfortable(tr::now),
				tr::lng_power_user_layout_compact(tr::now),
			},
			.initialSelection = int(PowerUser::Layout()),
			.callback = [=](int index) {
				const auto mode = PowerUser::LayoutMode(index);
				if (mode != PowerUser::Layout()) {
					PowerUser::SetLayout(mode);
					SuggestRestart(controller);
				}
			},
		});
	}));
}

void ShowBubbleRadiusBox(
		not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		SingleChoiceBox(box, {
			.title = tr::lng_power_user_bubble_radius(),
			.options = {
				tr::lng_power_user_bubble_round(tr::now),
				tr::lng_power_user_bubble_compact(tr::now),
			},
			.initialSelection = Ui::UseSmallBubbleRadius() ? 1 : 0,
			.callback = [=](int index) {
				const auto small = (index == 1);
				if (small != Ui::UseSmallBubbleRadius()) {
					Ui::SetUseSmallBubbleRadius(small);
					SuggestRestart(controller);
				}
			},
		});
	}));
}

void ShowSidebarWidthBox(
		not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		auto &settings = Core::App().settings();
		SingleChoiceBox(box, {
			.title = tr::lng_power_user_sidebar(),
			.options = {
				tr::lng_power_user_sidebar_narrow(tr::now),
				tr::lng_power_user_sidebar_balanced(tr::now),
				tr::lng_power_user_sidebar_wide(tr::now),
			},
			.initialSelection = SidebarWidthIndex(
				settings.dialogsWithChatWidthRatio()),
			.callback = [](int index) {
				constexpr auto kValues = std::array{ .28, 5. / 14., .42 };
				const auto ratio = kValues[std::clamp(index, 0, 2)];
				auto &settings = Core::App().settings();
				settings.updateDialogsWidthRatio(ratio, false);
				settings.updateDialogsWidthRatio(ratio, true);
				Core::App().saveSettingsDelayed();
			},
		});
	}));
}

void ShowTransparencyBox(
		not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		SingleChoiceBox(box, {
			.title = tr::lng_power_user_transparency(),
			.options = {
				tr::lng_power_user_transparency_off(tr::now),
				tr::lng_power_user_transparency_subtle(tr::now),
				tr::lng_power_user_transparency_glass(tr::now),
			},
			.initialSelection = int(PowerUser::Transparency()),
			.callback = [](int index) {
				PowerUser::SetTransparency(
					PowerUser::TransparencyMode(index));
			},
		});
	}));
}

void OpenMedia(
		not_null<Window::SessionController*> controller,
		PeerData *peer,
		Storage::SharedMediaType type) {
	if (!peer) {
		return;
	}
	controller->showSection(std::make_shared<Info::Memento>(
		not_null<PeerData*>(peer),
		Info::Section(type)));
}

void RunPaletteCommand(
		not_null<Window::SessionController*> controller,
		const PaletteCommand &command) {
	switch (command.action) {
	case PaletteAction::Search:
		Shortcuts::Launch(Shortcuts::Command::Search);
		break;
	case PaletteAction::SavedMessages:
		controller->showPeerHistory(controller->session().user());
		break;
	case PaletteAction::Contacts:
		Shortcuts::Launch(Shortcuts::Command::ShowContacts);
		break;
	case PaletteAction::MainSettings:
		controller->showSettings(MainId());
		break;
	case PaletteAction::PowerUserSettings:
		controller->showSettings(PowerUserId());
		break;
	case PaletteAction::ChatAppearance:
		controller->show(Box<BackgroundBox>(controller, command.peer));
		break;
	case PaletteAction::Workspaces:
		controller->showSettings(FoldersId());
		break;
	case PaletteAction::LocalArchive:
		controller->showSettings(LocalArchiveId());
		break;
	case PaletteAction::LocalBookmarks:
		ShowLocalBookmarks(controller);
		break;
	case PaletteAction::Photos:
		OpenMedia(controller, command.peer, Storage::SharedMediaType::Photo);
		break;
	case PaletteAction::Videos:
		OpenMedia(controller, command.peer, Storage::SharedMediaType::Video);
		break;
	case PaletteAction::Files:
		OpenMedia(controller, command.peer, Storage::SharedMediaType::File);
		break;
	case PaletteAction::Links:
		OpenMedia(controller, command.peer, Storage::SharedMediaType::Link);
		break;
	case PaletteAction::Music:
		OpenMedia(controller, command.peer, Storage::SharedMediaType::MusicFile);
		break;
	case PaletteAction::Voice:
		OpenMedia(
			controller,
			command.peer,
			Storage::SharedMediaType::RoundVoiceFile);
		break;
	}
}

void CommandPaletteBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	struct State {
		std::vector<PaletteCommand> commands;
		int first = -1;
	};
	const auto state = box->lifetime().make_state<State>();
	state->commands = {
		{
			tr::lng_power_user_command_search(tr::now),
			u"find global"_q,
			PaletteAction::Search,
		},
		{
			tr::lng_saved_messages(tr::now),
			u"self notes"_q,
			PaletteAction::SavedMessages,
		},
		{
			tr::lng_contacts_header(tr::now),
			u"people users"_q,
			PaletteAction::Contacts,
		},
		{
			tr::lng_menu_settings(tr::now),
			u"preferences"_q,
			PaletteAction::MainSettings,
		},
		{
			tr::lng_power_user_title(tr::now),
			u"advanced interface"_q,
			PaletteAction::PowerUserSettings,
		},
		{
			tr::lng_power_user_workspaces(tr::now),
			u"folders tabs workspaces"_q,
			PaletteAction::Workspaces,
		},
		{
			tr::lng_local_archive_title(tr::now),
			u"logging history retention"_q,
			PaletteAction::LocalArchive,
		},
		{
			tr::lng_local_bookmarks_title(tr::now),
			u"saved local"_q,
			PaletteAction::LocalBookmarks,
		},
	};
	const auto peer = controller->activeChatCurrent().peer();
	if (peer) {
		state->commands.push_back({
			tr::lng_power_user_chat_appearance(tr::now),
			u"wallpaper theme profile"_q,
			PaletteAction::ChatAppearance,
			peer,
		});
		state->commands.push_back({
			tr::lng_media_type_photos(tr::now),
			u"gallery images"_q,
			PaletteAction::Photos,
			peer,
		});
		state->commands.push_back({
			tr::lng_media_type_videos(tr::now),
			u"gallery media"_q,
			PaletteAction::Videos,
			peer,
		});
		state->commands.push_back({
			tr::lng_media_type_files(tr::now),
			u"documents gallery"_q,
			PaletteAction::Files,
			peer,
		});
		state->commands.push_back({
			tr::lng_media_type_links(tr::now),
			u"urls gallery"_q,
			PaletteAction::Links,
			peer,
		});
		state->commands.push_back({
			tr::lng_media_type_songs(tr::now),
			u"music gallery"_q,
			PaletteAction::Music,
			peer,
		});
		state->commands.push_back({
			tr::lng_media_type_audios(tr::now),
			u"voice video messages gallery"_q,
			PaletteAction::Voice,
			peer,
		});
	}

	box->setTitle(tr::lng_power_user_command_palette());
	box->setWidth(st::boxWideWidth);
	const auto content = box->verticalLayout();
	content->add(ModernUi::MakeHero(content, {
		.title = tr::lng_power_user_command_palette(),
		.description = tr::lng_power_user_command_palette_about(),
		.badge = rpl::single(CommandPaletteShortcutText()),
		.icon = &st::menuIconSearch,
	}), st::zgramBoxHeroMargin);
	const auto search = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::zgramSearchField,
			tr::lng_power_user_command_placeholder()),
		st::zgramSearchMargin);
	const auto results = content->add(object_ptr<Ui::VerticalLayout>(content));
	const auto invoke = [=](int index) {
		if (index < 0 || index >= int(state->commands.size())) {
			return;
		}
		const auto command = state->commands[index];
		box->closeBox();
		crl::on_main(controller, [=] {
			RunPaletteCommand(controller, command);
		});
	};
	const auto refresh = [=] {
		results->clear();
		state->first = -1;
		const auto query = search->getTextWithTags().text.simplified();
		auto shown = 0;
		for (auto index = 0, count = int(state->commands.size());
			index != count;
			++index) {
			const auto &command = state->commands[index];
			if (!query.isEmpty()
				&& !command.title.contains(query, Qt::CaseInsensitive)
				&& !command.keywords.contains(query, Qt::CaseInsensitive)) {
				continue;
			}
			if (state->first < 0) {
				state->first = index;
			}
			results->add(ModernUi::MakeActionCard(results, {
				.title = rpl::single(command.title),
				.description = rpl::single(PaletteGroupText(command.action)),
				.icon = PaletteIcon(command.action),
				.clicked = [=] { invoke(index); },
			}), st::zgramBoxCardMargin);
			++shown;
		}
		if (!shown) {
			results->add(ModernUi::MakeEmptyState(
				results,
				tr::lng_power_user_command_empty()),
				st::zgramBoxBottomMargin);
		}
	};
	refresh();
	search->changes(
	) | rpl::on_next(refresh, search->lifetime());
	search->submits(
	) | rpl::on_next([=] { invoke(state->first); }, search->lifetime());
	box->setFocusCallback([=] { search->setFocusFast(); });
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void BuildPowerUserSection(SectionBuilder &builder) {
	const auto controller = builder.controller();

	ModernUi::AddHero(builder, {
		.title = tr::lng_power_user_title(),
		.description = tr::lng_power_user_about(),
		.badge = tr::lng_power_user_device_badge(),
		.icon = &st::menuIconSettings,
	});
	ModernUi::AddSectionTitle(
		builder,
		tr::lng_power_user_appearance(),
		true);
	ModernUi::AddAction(builder, {
		.id = u"power_user/layout"_q,
		.title = tr::lng_power_user_layout(),
		.description = tr::lng_power_user_layout_about(),
		.badge = PowerUser::LayoutValue() | rpl::map(LayoutText),
		.icon = { &st::menuIconChatBubble },
		.clicked = [=] {
			if (controller) {
				ShowLayoutBox(controller);
			}
		},
		.keywords = { u"compact"_q, u"comfortable"_q, u"density"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/bubble_radius"_q,
		.title = tr::lng_power_user_bubble_radius(),
		.description = tr::lng_power_user_bubble_radius_about(),
		.badge = Ui::UseSmallBubbleRadiusValue() | rpl::map([](bool small) {
			return small
				? tr::lng_power_user_bubble_compact(tr::now)
				: tr::lng_power_user_bubble_round(tr::now);
		}),
		.icon = { &st::menuIconChatBubble },
		.clicked = [=] {
			if (controller) {
				ShowBubbleRadiusBox(controller);
			}
		},
		.keywords = { u"messages"_q, u"corners"_q, u"radius"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/sidebar"_q,
		.title = tr::lng_power_user_sidebar(),
		.description = tr::lng_power_user_sidebar_about(),
		.badge = rpl::single(
			Core::App().settings().dialogsWithChatWidthRatio()
		) | rpl::then(
			Core::App().settings().dialogsWithChatWidthRatioChanges()
		) | rpl::map([](float64 ratio) {
			return SidebarWidthText(SidebarWidthIndex(ratio));
		}),
		.icon = { &st::menuIconShowInFolder },
		.clicked = [=] {
			if (controller) {
				ShowSidebarWidthBox(controller);
			}
		},
		.keywords = { u"dialogs"_q, u"width"_q, u"column"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/transparency"_q,
		.title = tr::lng_power_user_transparency(),
		.description = tr::lng_power_user_transparency_about(),
		.badge = PowerUser::TransparencyValue()
			| rpl::map(TransparencyText),
		.icon = { &st::menuIconChangeColors },
		.clicked = [=] {
			if (controller) {
				ShowTransparencyBox(controller);
			}
		},
		.keywords = { u"blur"_q, u"glass"_q, u"opacity"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/colors"_q,
		.title = tr::lng_power_user_colors(),
		.description = tr::lng_power_user_colors_about(),
		.icon = { &st::menuIconChangeColors },
		.clicked = [=] {
			if (controller) {
				controller->showSettings(ChatId());
			}
		},
		.keywords = { u"accent"_q, u"theme"_q, u"color"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/wallpaper"_q,
		.title = tr::lng_power_user_wallpaper(),
		.description = tr::lng_power_user_wallpaper_about(),
		.icon = { &st::menuIconPhoto },
		.clicked = [=] {
			if (controller) {
				controller->show(Box<BackgroundBox>(controller));
			}
		},
		.keywords = { u"background"_q, u"image"_q, u"blur"_q },
	});

	ModernUi::AddSectionTitle(builder, tr::lng_power_user_actions());
	ModernUi::AddAction(builder, {
		.id = u"power_user/corner_reply"_q,
		.title = tr::lng_settings_chat_corner_reply(),
		.description = tr::lng_power_user_reply_about(),
		.icon = &st::menuIconReply,
		.toggled = Core::App().settings().cornerReplyValue(),
		.toggleCallback = [](bool checked) {
			Core::App().settings().setCornerReply(checked);
			Core::App().saveSettingsDelayed();
		},
		.keywords = { u"hover"_q, u"message"_q, u"reply"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/corner_reaction"_q,
		.title = tr::lng_settings_chat_corner_reaction(),
		.description = tr::lng_power_user_reaction_about(),
		.icon = &st::menuIconReactions,
		.toggled = Core::App().settings().cornerReactionValue(),
		.toggleCallback = [](bool checked) {
			Core::App().settings().setCornerReaction(checked);
			Core::App().saveSettingsDelayed();
		},
		.keywords = { u"hover"_q, u"message"_q, u"reaction"_q },
	});

	ModernUi::AddSectionTitle(builder, tr::lng_power_user_tools());
	ModernUi::AddAction(builder, {
		.id = u"power_user/command_palette"_q,
		.title = tr::lng_power_user_command_palette(),
		.description = tr::lng_power_user_command_palette_about(),
		.badge = rpl::single(CommandPaletteShortcutText()),
		.icon = { &st::menuIconSearch },
		.clicked = [=] {
			if (controller) {
				ShowCommandPalette(controller);
			}
		},
		.keywords = { u"commands"_q, u"keyboard"_q, u"search"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/workspaces"_q,
		.title = tr::lng_power_user_workspaces(),
		.description = tr::lng_power_user_workspaces_about(),
		.icon = { &st::menuIconShowInFolder },
		.clicked = [=] {
			if (controller) {
				controller->showSettings(FoldersId());
			}
		},
		.keywords = { u"workspaces"_q, u"folders"_q, u"tabs"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/archive"_q,
		.title = tr::lng_local_archive_title(),
		.description = tr::lng_power_user_archive_about(),
		.icon = { &st::menuIconGroupLog },
		.tone = ModernUi::Tone::Accent,
		.clicked = [=] {
			if (controller) {
				controller->showSettings(LocalArchiveId());
			}
		},
		.keywords = { u"archive"_q, u"messages"_q, u"logging"_q },
	});
	ModernUi::AddAction(builder, {
		.id = u"power_user/bookmarks"_q,
		.title = tr::lng_local_bookmarks_title(),
		.description = tr::lng_power_user_bookmarks_about(),
		.icon = { &st::menuIconFave },
		.clicked = [=] {
			if (controller) {
				ShowLocalBookmarks(controller);
			}
		},
		.keywords = { u"bookmarks"_q, u"saved"_q, u"local"_q },
	});
}

class PowerUserSettings : public Section<PowerUserSettings> {
public:
	PowerUserSettings(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

const auto kMeta = BuildHelper({
	.id = PowerUserSettings::Id(),
	.parentId = MainId(),
	.title = &tr::lng_power_user_title,
	.icon = &st::menuIconSettings,
}, [](SectionBuilder &builder) {
	BuildPowerUserSection(builder);
});

const SectionBuildMethod kPowerUserSection = kMeta.build;

PowerUserSettings::PowerUserSettings(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto inner = content->add(
		object_ptr<Ui::VerticalLayout>(content),
		st::zgramPagePadding);
	build(inner, kPowerUserSection);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> PowerUserSettings::title() {
	return tr::lng_power_user_title();
}

} // namespace

Type PowerUserId() {
	return PowerUserSettings::Id();
}

void ShowCommandPalette(
		not_null<Window::SessionController*> controller) {
	controller->show(Box(CommandPaletteBox, controller));
}

} // namespace Settings
