/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/object_ptr.h"
#include "ui/style/style_core_types.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"

#include <rpl/producer.h>

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Settings::ModernUi {

enum class Tone {
	Neutral,
	Accent,
	Attention,
};

class Panel final : public Ui::PaddingWrap<Ui::VerticalLayout> {
public:
	Panel(QWidget *parent, Tone tone = Tone::Neutral);

	[[nodiscard]] not_null<Ui::VerticalLayout*> body();

protected:
	void paintEvent(QPaintEvent *) override;
	void wrappedNaturalWidthUpdated(int) override;

private:
	Tone _tone = Tone::Neutral;

};

class Badge final : public Ui::RpWidget {
public:
	Badge(
		QWidget *parent,
		rpl::producer<QString> text,
		Tone tone = Tone::Accent);

	[[nodiscard]] bool empty() const;
	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

protected:
	void paintEvent(QPaintEvent *) override;
	int resizeGetHeight(int) override;

private:
	void refreshSize();

	QString _text;
	Tone _tone = Tone::Accent;

};

struct HeroArgs {
	rpl::producer<QString> title;
	rpl::producer<QString> description;
	rpl::producer<QString> badge;
	const style::icon *icon = nullptr;
	Tone tone = Tone::Accent;
};

struct ActionArgs {
	QString id;
	rpl::producer<QString> title;
	rpl::producer<QString> description;
	rpl::producer<QString> badge;
	const style::icon *icon = nullptr;
	Tone tone = Tone::Neutral;
	Fn<void()> clicked;
	rpl::producer<bool> toggled;
	Fn<void(bool)> toggleCallback;
	QStringList keywords;
};

[[nodiscard]] object_ptr<Ui::RpWidget> MakeHero(
	QWidget *parent,
	HeroArgs &&args);

[[nodiscard]] object_ptr<Ui::RpWidget> MakeActionCard(
	QWidget *parent,
	ActionArgs &&args);

[[nodiscard]] object_ptr<Ui::RpWidget> MakeEmptyState(
	QWidget *parent,
	rpl::producer<QString> text,
	Tone tone = Tone::Neutral);

void AddHero(Builder::SectionBuilder &builder, HeroArgs &&args);
void AddSectionTitle(
	Builder::SectionBuilder &builder,
	rpl::producer<QString> title,
	bool first = false);
Ui::RpWidget *AddAction(
	Builder::SectionBuilder &builder,
	ActionArgs &&args);
void AddEmptyState(
	Builder::SectionBuilder &builder,
	rpl::producer<QString> text,
	Tone tone = Tone::Neutral);

} // namespace Settings::ModernUi
