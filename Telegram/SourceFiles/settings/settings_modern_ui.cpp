/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_modern_ui.h"

#include "settings/settings_builder.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QPaintEvent>

#include <algorithm>
#include <memory>
#include <utility>

namespace Settings::ModernUi {
namespace {

[[nodiscard]] QColor ToneColor(Tone tone) {
	switch (tone) {
	case Tone::Neutral: return st::windowSubTextFg->c;
	case Tone::Accent: return st::windowActiveTextFg->c;
	case Tone::Attention: return st::attentionButtonFg->c;
	}
	Unexpected("ModernUi::Tone value.");
}

[[nodiscard]] QColor WithOpacity(QColor color, float64 opacity) {
	color.setAlphaF(color.alphaF() * opacity);
	return color;
}

void PaintSurface(
		QPainter &p,
		QRect rect,
		Tone tone,
		bool over,
		int radius,
		int border,
		float64 toneOpacity,
		float64 borderOpacity) {
	if (rect.isEmpty()) {
		return;
	}
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(over ? st::windowBgRipple : st::windowBgOver);
	p.drawRoundedRect(rect, radius, radius);

	const auto toneColor = ToneColor(tone);
	p.setBrush(WithOpacity(toneColor, toneOpacity));
	p.drawRoundedRect(rect, radius, radius);
	if (border > 0) {
		const auto inset = border / 2.;
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(
			WithOpacity(toneColor, borderOpacity),
			border));
		p.drawRoundedRect(
			QRectF(rect).adjusted(inset, inset, -inset, -inset),
			radius,
			radius);
	}
}

[[nodiscard]] rpl::producer<QString> EnsureText(
		rpl::producer<QString> text) {
	return text ? std::move(text) : rpl::single(QString());
}

class Hero final : public Ui::RpWidget {
public:
	Hero(QWidget *parent, HeroArgs &&args);

protected:
	void paintEvent(QPaintEvent *) override;
	int resizeGetHeight(int newWidth) override;

private:
	const style::icon *_icon = nullptr;
	Tone _tone = Tone::Accent;
	not_null<Ui::FlatLabel*> _title;
	not_null<Ui::FlatLabel*> _description;
	not_null<Badge*> _badge;
	QRect _iconRect;

};

class ActionCard final : public Ui::RippleButton {
public:
	ActionCard(QWidget *parent, ActionArgs &&args);

	QString accessibilityName() override;
	QString accessibilityDescription() override;
	Ui::AccessibilityState accessibilityState() const override;
	int accessibilityChildCount() const override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
	QAccessible::Role accessibilityRole() override;
#endif

protected:
	void paintEvent(QPaintEvent *) override;
	void onStateChanged(State was, StateChangeSource source) override;
	int resizeGetHeight(int newWidth) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	[[nodiscard]] QRect toggleRect() const;
	void refreshLayout();

	const style::icon *_icon = nullptr;
	Tone _tone = Tone::Neutral;
	not_null<Ui::FlatLabel*> _title;
	not_null<Ui::FlatLabel*> _description;
	not_null<Badge*> _badge;
	std::unique_ptr<Ui::ToggleView> _toggle;
	Fn<void(bool)> _toggleCallback;
	QString _accessibilityTitle;
	QString _accessibilityDescription;
	QRect _iconRect;

};

Hero::Hero(QWidget *parent, HeroArgs &&args)
: RpWidget(parent)
, _icon(args.icon)
, _tone(args.tone)
, _title(Ui::CreateChild<Ui::FlatLabel>(
	this,
	EnsureText(std::move(args.title)),
	st::zgramHeroTitle))
, _description(Ui::CreateChild<Ui::FlatLabel>(
	this,
	EnsureText(std::move(args.description)),
	st::zgramHeroAbout))
, _badge(Ui::CreateChild<Badge>(
	this,
	EnsureText(std::move(args.badge)),
	args.tone)) {
	_title->show();
	_description->show();
	_badge->show();
}

void Hero::paintEvent(QPaintEvent *) {
	auto p = Painter(this);
	PaintSurface(
		p,
		rect(),
		_tone,
		false,
		st::zgramHeroRadius,
		st::zgramHeroBorder,
		st::zgramHeroAccentOpacity,
		st::zgramHeroBorderOpacity);
	if (_icon) {
		const auto color = ToneColor(_tone);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(WithOpacity(color, st::zgramBadgeToneOpacity));
		p.drawRoundedRect(
			_iconRect,
			st::zgramHeroIconRadius,
			st::zgramHeroIconRadius);
		_icon->paintInCenter(p, _iconRect, color);
	}
}

int Hero::resizeGetHeight(int newWidth) {
	const auto padding = st::zgramHeroPadding;
	const auto iconWidth = _icon
		? st::zgramHeroIconSize + st::zgramHeroIconSkip
		: 0;
	const auto textLeft = padding.left() + iconWidth;
	const auto available = std::max(
		newWidth - textLeft - padding.right(),
		st::zgramHeroIconSize);
	auto top = padding.top();
	_badge->resizeToNaturalWidth(available);
	_badge->moveToLeft(textLeft, top, newWidth);
	top += _badge->height() + st::zgramHeroBadgeSkip;
	_title->resizeToWidth(available);
	_title->moveToLeft(textLeft, top, newWidth);
	top += _title->height() + st::zgramHeroTitleSkip;
	_description->resizeToWidth(available);
	_description->moveToLeft(textLeft, top, newWidth);
	top += _description->height();
	_iconRect = style::rtlrect(
		padding.left(),
		padding.top(),
		st::zgramHeroIconSize,
		st::zgramHeroIconSize,
		newWidth);
	const auto iconBottom = padding.top() + st::zgramHeroIconSize;
	return std::max(top, iconBottom) + padding.bottom();
}

ActionCard::ActionCard(QWidget *parent, ActionArgs &&args)
: RippleButton(parent, st::zgramCardRipple)
, _icon(args.icon)
, _tone(args.tone)
, _title(Ui::CreateChild<Ui::FlatLabel>(
	this,
	EnsureText(rpl::duplicate(args.title)),
	st::zgramCardTitle))
, _description(Ui::CreateChild<Ui::FlatLabel>(
	this,
	EnsureText(rpl::duplicate(args.description)),
	st::zgramCardDescription))
, _badge(Ui::CreateChild<Badge>(
	this,
	EnsureText(std::move(args.badge)),
	args.tone))
, _toggleCallback(std::move(args.toggleCallback)) {
	_title->show();
	_description->show();
	_badge->show();
	if (_tone == Tone::Attention) {
		_title->setTextColorOverride(st::attentionButtonFg->c);
	}
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_description->setAttribute(Qt::WA_TransparentForMouseEvents);
	_badge->setAttribute(Qt::WA_TransparentForMouseEvents);
	_badge->naturalWidthValue(
	) | rpl::on_next([=] {
		refreshLayout();
	}, lifetime());
	if (args.title) {
		std::move(
			args.title
		) | rpl::on_next([=](QString title) {
			_accessibilityTitle = std::move(title);
			accessibilityNameChanged();
		}, lifetime());
	}
	if (args.description) {
		std::move(
			args.description
		) | rpl::on_next([=](QString description) {
			_accessibilityDescription = std::move(description);
			accessibilityDescriptionChanged();
		}, lifetime());
	}
	if (args.toggled) {
		_toggle = std::make_unique<Ui::ToggleView>(
			st::defaultSettingsToggle,
			false,
			[this] { rtlupdate(toggleRect()); });
		std::move(
			args.toggled
		) | rpl::on_next([=](bool checked) {
			_toggle->setChecked(checked, anim::type::normal);
		}, lifetime());
		_toggle->checkedChanges(
		) | rpl::on_next([=] {
			accessibilityStateChanged({ .checked = true });
		}, lifetime());
		setClickedCallback([=] {
			const auto checked = !_toggle->checked();
			_toggle->setChecked(checked, anim::type::normal);
			if (_toggleCallback) {
				_toggleCallback(checked);
			}
		});
	} else if (args.clicked) {
		setClickedCallback(std::move(args.clicked));
	}
	setPointerCursor(true);
}

QString ActionCard::accessibilityName() {
	return _accessibilityTitle;
}

QString ActionCard::accessibilityDescription() {
	return _accessibilityDescription;
}

Ui::AccessibilityState ActionCard::accessibilityState() const {
	return _toggle
		? Ui::AccessibilityState{
			.checkable = true,
			.checked = _toggle->checked(),
		}
		: Ui::RippleButton::accessibilityState();
}

int ActionCard::accessibilityChildCount() const {
	return 0;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
QAccessible::Role ActionCard::accessibilityRole() {
	return _toggle
		? QAccessible::Role::Switch
		: QAccessible::Role::Button;
}
#endif

void ActionCard::paintEvent(QPaintEvent *) {
	auto p = Painter(this);
	const auto over = (isOver() || isDown()) && !isDisabled();
	PaintSurface(
		p,
		rect(),
		_tone,
		over,
		st::zgramCardRadius,
		st::zgramCardBorder,
		over
			? st::zgramCardToneOverOpacity
			: st::zgramCardToneOpacity,
		st::zgramCardBorderOpacity);
	paintRipple(p, 0, 0);
	if (_icon) {
		const auto color = ToneColor(_tone == Tone::Neutral
			? Tone::Accent
			: _tone);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(WithOpacity(color, st::zgramBadgeToneOpacity));
		p.drawRoundedRect(
			_iconRect,
			st::zgramCardIconRadius,
			st::zgramCardIconRadius);
		_icon->paintInCenter(p, _iconRect, color);
	}
	if (_toggle) {
		const auto toggle = toggleRect();
		_toggle->paint(p, toggle.left(), toggle.top(), width());
	}
}

void ActionCard::onStateChanged(
		State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if (_toggle) {
		_toggle->setStyle(isOver()
			? st::defaultSettingsToggleOver
			: st::defaultSettingsToggle);
	}
	update();
}

int ActionCard::resizeGetHeight(int newWidth) {
	const auto padding = st::zgramCardPadding;
	const auto iconWidth = _icon
		? st::zgramCardIconSize + st::zgramCardIconSkip
		: 0;
	const auto textLeft = padding.left() + iconWidth;
	auto trailing = padding.right();
	if (_toggle) {
		trailing += _toggle->getSize().width();
	} else if (!_badge->empty()) {
		_badge->resizeToNaturalWidth(newWidth);
		trailing += _badge->width();
	}
	if (_toggle || !_badge->empty()) {
		trailing += st::zgramCardTrailingSkip;
	}
	const auto available = std::max(
		newWidth - textLeft - trailing,
		st::zgramCardBorder);
	_title->resizeToWidth(available);
	_description->resizeToWidth(available);
	const auto textHeight = _title->height()
		+ st::zgramCardTextSkip
		+ _description->height();
	const auto textTop = (st::zgramCardHeight - textHeight) / 2;
	_title->moveToLeft(textLeft, textTop, newWidth);
	_description->moveToLeft(
		textLeft,
		textTop + _title->height() + st::zgramCardTextSkip,
		newWidth);
	_iconRect = style::rtlrect(
		padding.left(),
		(st::zgramCardHeight - st::zgramCardIconSize) / 2,
		st::zgramCardIconSize,
		st::zgramCardIconSize,
		newWidth);
	if (!_badge->empty() && !_toggle) {
		_badge->moveToRight(
			padding.right(),
			(st::zgramCardHeight - _badge->height()) / 2,
			newWidth);
	}
	return st::zgramCardHeight;
}

QImage ActionCard::prepareRippleMask() const {
	return Ui::RippleAnimation::MaskByDrawer(size(), false, [&](QPainter &p) {
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			rect(),
			st::zgramCardRadius,
			st::zgramCardRadius);
	});
}

QPoint ActionCard::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QRect ActionCard::toggleRect() const {
	if (!_toggle) {
		return QRect();
	}
	const auto size = _toggle->getSize();
	return style::rtlrect(
		width() - st::zgramCardPadding.right() - size.width(),
		(height() - size.height()) / 2,
		size.width(),
		size.height(),
		width());
}

void ActionCard::refreshLayout() {
	const auto current = widthNoMargins();
	if (current > 0) {
		resizeToWidth(current);
	}
	update();
}

} // namespace

Panel::Panel(QWidget *parent, Tone tone)
: PaddingWrap(
	parent,
	object_ptr<Ui::VerticalLayout>(parent),
	st::zgramPanelPadding)
, _tone(tone) {
	setNaturalWidth(-1);
}

not_null<Ui::VerticalLayout*> Panel::body() {
	return entity();
}

void Panel::paintEvent(QPaintEvent *) {
	auto p = Painter(this);
	PaintSurface(
		p,
		rect(),
		_tone,
		false,
		st::zgramPanelRadius,
		st::zgramPanelBorder,
		st::zgramPanelToneOpacity,
		st::zgramPanelBorderOpacity);
}

void Panel::wrappedNaturalWidthUpdated(int) {
	setNaturalWidth(-1);
}

Badge::Badge(
	QWidget *parent,
	rpl::producer<QString> text,
	Tone tone)
: RpWidget(parent)
, _tone(tone) {
	std::move(
		text
	) | rpl::on_next([=](QString value) {
		_text = std::move(value);
		accessibilityNameChanged();
		refreshSize();
		update();
	}, lifetime());
}

bool Badge::empty() const {
	return _text.isEmpty();
}

QAccessible::Role Badge::accessibilityRole() {
	return QAccessible::Role::StaticText;
}

QString Badge::accessibilityName() {
	return _text;
}

void Badge::paintEvent(QPaintEvent *) {
	if (_text.isEmpty()) {
		return;
	}
	auto p = Painter(this);
	const auto color = ToneColor(_tone);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(WithOpacity(color, st::zgramBadgeToneOpacity));
	p.drawRoundedRect(rect(), height() / 2., height() / 2.);
	p.setFont(st::zgramBadgeFont);
	p.setPen(color);
	p.drawTextLeft(
		st::zgramBadgePadding.left(),
		st::zgramBadgePadding.top(),
		width(),
		_text);
}

int Badge::resizeGetHeight(int) {
	return _text.isEmpty() ? 0 : st::zgramBadgeHeight;
}

void Badge::refreshSize() {
	const auto width = _text.isEmpty()
		? 0
		: st::zgramBadgeFont->width(_text)
			+ st::zgramBadgePadding.left()
			+ st::zgramBadgePadding.right();
	setNaturalWidth(width);
	resize(width, _text.isEmpty() ? 0 : st::zgramBadgeHeight);
}

object_ptr<Ui::RpWidget> MakeHero(QWidget *parent, HeroArgs &&args) {
	return object_ptr<Hero>(parent, std::move(args));
}

object_ptr<Ui::RpWidget> MakeActionCard(
		QWidget *parent,
		ActionArgs &&args) {
	return object_ptr<ActionCard>(parent, std::move(args));
}

object_ptr<Ui::RpWidget> MakeEmptyState(
		QWidget *parent,
		rpl::producer<QString> text,
		Tone tone) {
	auto result = object_ptr<Panel>(parent, tone);
	result->body()->add(object_ptr<Ui::FlatLabel>(
		result->body(),
		EnsureText(std::move(text)),
		st::zgramHeroAbout));
	return result;
}

void AddHero(Builder::SectionBuilder &builder, HeroArgs &&args) {
	const auto factory = [&](not_null<Ui::VerticalLayout*> container) {
		return MakeHero(container, std::move(args));
	};
	builder.addControl({
		.factory = factory,
		.title = rpl::single(QString()),
		.margin = st::zgramHeroMargin,
	});
}

void AddSectionTitle(
		Builder::SectionBuilder &builder,
		rpl::producer<QString> title,
		bool first) {
	const auto factory = [&](not_null<Ui::VerticalLayout*> container) {
		return object_ptr<Ui::FlatLabel>(
			container,
			std::move(title),
			st::zgramSectionTitle);
	};
	builder.addControl({
		.factory = factory,
		.title = rpl::single(QString()),
		.margin = first
			? st::zgramSectionTitleMargin
			: st::zgramSectionTitleTop,
	});
}

Ui::RpWidget *AddAction(
		Builder::SectionBuilder &builder,
		ActionArgs &&args) {
	auto searchTitle = rpl::duplicate(args.title);
	const auto searchIcon = args.icon;
	const auto factory = [&](not_null<Ui::VerticalLayout*> container) {
		return MakeActionCard(container, std::move(args));
	};
	return builder.addControl({
		.factory = factory,
		.id = std::move(args.id),
		.title = std::move(searchTitle),
		.margin = st::zgramCardMargin,
		.highlight = { .radius = st::zgramCardRadius },
		.keywords = std::move(args.keywords),
		.searchIcon = { searchIcon },
	});
}

void AddEmptyState(
		Builder::SectionBuilder &builder,
		rpl::producer<QString> text,
		Tone tone) {
	const auto factory = [&](not_null<Ui::VerticalLayout*> container) {
		return MakeEmptyState(container, std::move(text), tone);
	};
	builder.addControl({
		.factory = factory,
		.title = rpl::single(QString()),
		.margin = st::zgramCardMargin,
	});
}

} // namespace Settings::ModernUi
