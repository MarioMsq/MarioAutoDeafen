#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/Notification.hpp>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

// ============================================================
//  Логика autodeafen
// ============================================================
namespace ad {

	struct Bind {
		int key = 0;
		bool ctrl = false;
		bool shift = false;
		bool alt = false;
	};

	inline bool g_deafened = false;

	// ---------- сохранённые настройки ----------
	inline bool enabled() { return Mod::get()->getSavedValue<bool>("enabled", false); }
	inline int percent() { return Mod::get()->getSavedValue<int>("percent", 50); }
	inline bool undeafenEnabled() { return Mod::get()->getSavedValue<bool>("undeafen-enabled", false); }
	inline int undeafenPercent() { return Mod::get()->getSavedValue<int>("undeafen-percent", 80); }
	inline bool startposEnabled() { return Mod::get()->getSavedValue<bool>("startpos", true); }

	// Цвет надписи «AD» на кнопке в меню паузы: зелёный = включено, серый = выключено.
	// Обновляется сразу при включении/выключении мода.
	inline void refreshIndicator() {
		auto scene = CCDirector::get()->getRunningScene();
		if (!scene) return;
		auto node = scene->getChildByIDRecursive("autodeafen-label"_spr);
		if (auto lbl = typeinfo_cast<CCLabelBMFont*>(node)) {
			lbl->setColor(enabled() ? ccColor3B{110, 255, 110} : ccColor3B{190, 190, 190});
		}
	}

	// prefix "" = клавиша Discord, prefix "toggle-" = клавиша вкл/выкл мода
	inline Bind getBind(std::string const& prefix) {
		auto m = Mod::get();
		Bind b;
		b.key = m->getSavedValue<int>(prefix + "key", 0);
		b.ctrl = m->getSavedValue<bool>(prefix + "ctrl", false);
		b.shift = m->getSavedValue<bool>(prefix + "shift", false);
		b.alt = m->getSavedValue<bool>(prefix + "alt", false);
		return b;
	}

	inline void setBind(std::string const& prefix, Bind const& b) {
		auto m = Mod::get();
		m->setSavedValue<int>(prefix + "key", b.key);
		m->setSavedValue<bool>(prefix + "ctrl", b.ctrl);
		m->setSavedValue<bool>(prefix + "shift", b.shift);
		m->setSavedValue<bool>(prefix + "alt", b.alt);
	}

	inline std::string bindToString(Bind const& b) {
		if (!b.key) return "Not set";
		std::string s;
		if (b.ctrl) s += "Ctrl+";
		if (b.shift) s += "Shift+";
		if (b.alt) s += "Alt+";
		auto name = CCKeyboardDispatcher::get()->keyToString(static_cast<enumKeyCodes>(b.key));
		if (name && name[0]) s += name;
		else s += "Key " + std::to_string(b.key);
		return s;
	}

	// ---------- нажатие клавиши Discord ----------
	inline void pressBind() {
#ifdef GEODE_IS_WINDOWS
		auto b = getBind("");
		if (!b.key) return;

		std::thread([b] {
			auto isExtended = [](WORD vk) {
				return (vk >= 33 && vk <= 46) || vk == 144 || vk == 165 || vk == 163;
			};
			auto make = [&](WORD vk, bool up) {
				INPUT in{};
				in.type = INPUT_KEYBOARD;
				in.ki.wVk = vk;
				in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
				in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (isExtended(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
				return in;
			};

			std::vector<WORD> keys;
			if (b.ctrl) keys.push_back(VK_CONTROL);
			if (b.shift) keys.push_back(VK_SHIFT);
			if (b.alt) keys.push_back(VK_MENU);
			keys.push_back(static_cast<WORD>(b.key));

			std::vector<INPUT> down, up;
			for (auto vk : keys) down.push_back(make(vk, false));
			for (auto it = keys.rbegin(); it != keys.rend(); ++it) up.push_back(make(*it, true));

			SendInput(static_cast<UINT>(down.size()), down.data(), sizeof(INPUT));
			Sleep(40);
			SendInput(static_cast<UINT>(up.size()), up.data(), sizeof(INPUT));
		}).detach();
#endif
	}

	inline void deafen() {
		if (g_deafened) return;
		g_deafened = true;
		pressBind();
	}

	inline void undeafen() {
		if (!g_deafened) return;
		g_deafened = false;
		pressBind();
	}

	// ---------- вкл/выкл мода по клавише ----------
	inline void toggleEnabled() {
		bool now = !enabled();
		Mod::get()->setSavedValue<bool>("enabled", now);
		if (!now) undeafen(); // выключили мод, пока звук отключён — возвращаем звук
		refreshIndicator();
		Notification::create(
			now ? "AutoDeafen: ON" : "AutoDeafen: OFF",
			now ? NotificationIcon::Success : NotificationIcon::Error,
			1.f
		)->show();
	}

#ifdef GEODE_IS_WINDOWS
	inline bool isHeld(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
#endif

	// Вызывается каждый кадр
	inline void pollToggleBind() {
#ifdef GEODE_IS_WINDOWS
		static bool wasDown = false;

		auto b = getBind("toggle-");
		if (!b.key) { wasDown = false; return; }

		// реагируем, только если окно игры активно
		DWORD pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &pid);
		if (pid != GetCurrentProcessId()) { wasDown = false; return; }

		bool down = isHeld(b.key)
			&& isHeld(VK_CONTROL) == b.ctrl
			&& isHeld(VK_SHIFT) == b.shift
			&& isHeld(VK_MENU) == b.alt;

		if (down && !wasDown) toggleEnabled();
		wasDown = down;
#endif
	}

	// ---------- стартпосы ----------
	struct StartposInfo {
		std::string levelKey;
		int count = 0;
		int current = -1; // индекс стартпоса, с которого запущен уровень (-1 = с начала)
	};

	// Стартпосы нумеруются слева направо (по X)
	inline StartposInfo startposInfo(PlayLayer* pl) {
		StartposInfo info;
		if (!pl || !pl->m_level) return info;

		int id = pl->m_level->m_levelID.value();
		info.levelKey = id != 0 ? std::to_string(id) : std::string(pl->m_level->m_levelName);

		std::vector<GameObject*> sps;
		if (pl->m_objects) {
			for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
				if (obj && obj->m_objectID == 31) sps.push_back(obj);
			}
		}
		std::sort(sps.begin(), sps.end(), [](GameObject* a, GameObject* b) {
			return a->getPositionX() < b->getPositionX();
		});

		info.count = static_cast<int>(sps.size());
		if (pl->m_startPosObject) {
			for (int i = 0; i < info.count; i++) {
				if (sps[i] == pl->m_startPosObject) { info.current = i; break; }
			}
		}
		return info;
	}

	inline std::string startposKey(std::string const& levelKey, int idx) {
		return "sp:" + levelKey + ":" + std::to_string(idx);
	}

	// Процент deafen для данного стартпоса (или общий, если свой не задан)
	inline int deafenPercentFor(std::string const& levelKey, int idx) {
		if (idx < 0) return percent();
		int v = Mod::get()->getSavedValue<int>(startposKey(levelKey, idx), -1);
		return v >= 0 ? v : percent();
	}

	inline void setStartposPercent(std::string const& levelKey, int idx, int value) {
		Mod::get()->setSavedValue<int>(startposKey(levelKey, idx), value);
	}

	// ---------- процент ровно такой, как показывает игра ----------
	// В игре (2.2) процент считается не по простой формуле, поэтому берём число
	// прямо из надписи с процентами в верхней части экрана.
	inline float parsePercent(std::string text) {
		if (text.empty() || text.back() != '%') return -1.f;
		text.pop_back();
		if (text.empty()) return -1.f;
		for (char c : text) {
			if (!((c >= '0' && c <= '9') || c == '.')) return -1.f;
		}
		try { return std::stof(text); } catch (...) { return -1.f; }
	}

	inline void searchPercentLabels(CCNode* node, std::vector<CCLabelBMFont*>& out) {
		if (!node) return;
		if (auto lbl = typeinfo_cast<CCLabelBMFont*>(node)) {
			if (parsePercent(lbl->getString()) >= 0.f) out.push_back(lbl);
		}
		auto children = node->getChildren();
		if (!children) return;
		for (auto child : CCArrayExt<CCNode*>(children)) {
			searchPercentLabels(child, out);
		}
	}

	// Надпись с процентами: целиком "53%" (или "53.2%"), вверху экрана ближе к центру
	inline CCLabelBMFont* findPercentLabel() {
		auto scene = CCDirector::get()->getRunningScene();
		if (!scene) return nullptr;

		std::vector<CCLabelBMFont*> labels;
		searchPercentLabels(scene, labels);

		auto win = CCDirector::get()->getWinSize();
		CCLabelBMFont* best = nullptr;
		float bestScore = 1e9f;
		for (auto lbl : labels) {
			auto parent = lbl->getParent();
			if (!parent) continue;
			auto p = parent->convertToWorldSpace(lbl->getPosition());
			if (p.y < win.height * 0.8f) continue;
			float score = std::abs(p.x - win.width / 2.f);
			if (score < bestScore) { bestScore = score; best = lbl; }
		}
		return best;
	}

	// ---------- расстановка кнопки в меню паузы ----------
	inline CCRect worldRect(CCNode* n) {
		auto bb = n->boundingBox();
		auto p = n->getParent();
		if (!p) return bb;
		auto o = p->convertToWorldSpace(bb.origin);
		auto e = p->convertToWorldSpace({bb.origin.x + bb.size.width, bb.origin.y + bb.size.height});
		return CCRect(o.x, o.y, e.x - o.x, e.y - o.y);
	}

	inline void collectItems(CCNode* node, CCNode* ignore, std::vector<CCRect>& out) {
		if (!node || node == ignore || !node->isVisible()) return;

		bool relevant = false;
		if (typeinfo_cast<CCMenuItem*>(node)) {
			relevant = true;
		}
		else if (auto spr = typeinfo_cast<CCSprite*>(node)) {
			relevant = spr->getOpacity() > 20; // почти прозрачные не считаем
		}
		else if (typeinfo_cast<CCLabelBMFont*>(node)) {
			relevant = true;
		}

		if (relevant) {
			auto r = worldRect(node);
			// огромные элементы (фон, затемнение) и совсем крошечные не считаем
			if (r.size.width > 6.f && r.size.height > 6.f && r.size.width < 120.f && r.size.height < 120.f) {
				out.push_back(r);
			}
		}

		auto children = node->getChildren();
		if (!children) return;
		for (auto child : CCArrayExt<CCNode*>(children)) {
			collectItems(child, ignore, out);
		}
	}

	// Ищет свободное место справа сверху (потом по верхнему краю, потом слева)
	inline void placeButton(CCNode* layer, CCNode* menu) {
		auto win = CCDirector::get()->getWinSize();

		std::vector<CCRect> others;
		collectItems(layer, menu, others);

		const float half = 19.f;
		auto isFree = [&](CCPoint p) {
			CCRect r(p.x - half, p.y - half, half * 2.f, half * 2.f);
			for (auto const& o : others) {
				if (r.intersectsRect(o)) return false;
			}
			return true;
		};

		std::vector<CCPoint> candidates;
		for (float y = win.height - 37.f; y > 60.f; y -= 38.f) candidates.push_back({win.width - 37.f, y});
		for (float x = win.width - 75.f; x > 40.f; x -= 38.f) candidates.push_back({x, win.height - 37.f});
		for (float y = win.height - 37.f; y > 60.f; y -= 38.f) candidates.push_back({37.f, y});

		CCPoint chosen = {win.width - 37.f, win.height - 75.f};
		for (auto const& c : candidates) {
			if (isFree(c)) { chosen = c; break; }
		}
		menu->setPosition(layer->convertToNodeSpace(chosen));
	}
}

// ============================================================
//  Окно: проценты для каждого стартпоса
// ============================================================
class StartposPopup : public Popup {
protected:
	ad::StartposInfo m_info;
	int m_index = 0;
	bool m_refreshing = false;
	CCLabelBMFont* m_indexLabel = nullptr;
	TextInput* m_input = nullptr;

	bool init() {
		if (!Popup::init(280.f, 210.f)) return false;
		this->setTitle("Startpos percents");

		m_info = ad::startposInfo(PlayLayer::get());

		if (m_info.count <= 0) {
			auto none = CCLabelBMFont::create("No startpos in this level", "chatFont.fnt");
			m_mainLayer->addChildAtPosition(none, Anchor::Center, {0.f, 0.f});
			return true;
		}

		m_index = m_info.current >= 0 ? m_info.current : 0;

		// --- стрелки и номер стартпоса ---
		auto leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
		leftSpr->setScale(0.8f);
		auto leftBtn = CCMenuItemSpriteExtra::create(leftSpr, this, menu_selector(StartposPopup::onPrev));
		m_buttonMenu->addChildAtPosition(leftBtn, Anchor::Center, {-100.f, 40.f});

		auto rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
		rightSpr->setFlipX(true);
		rightSpr->setScale(0.8f);
		auto rightBtn = CCMenuItemSpriteExtra::create(rightSpr, this, menu_selector(StartposPopup::onNext));
		m_buttonMenu->addChildAtPosition(rightBtn, Anchor::Center, {100.f, 40.f});

		m_indexLabel = CCLabelBMFont::create("", "bigFont.fnt");
		m_indexLabel->setScale(0.5f);
		m_mainLayer->addChildAtPosition(m_indexLabel, Anchor::Center, {0.f, 40.f});

		// --- процент ---
		auto label = CCLabelBMFont::create("Deafen at", "chatFont.fnt");
		label->setAnchorPoint({0.f, 0.5f});
		label->setScale(0.9f);
		m_mainLayer->addChildAtPosition(label, Anchor::Center, {-88.f, -5.f});

		m_input = TextInput::create(60.f, "0-100", "bigFont.fnt");
		m_input->setFilter("0123456789");
		m_input->setMaxCharCount(3);
		m_input->setCallback([this](std::string const& text) {
			if (m_refreshing || text.empty()) return;
			int v = 0;
			try { v = std::stoi(text); } catch (...) { return; }
			v = std::clamp(v, 0, 100);
			ad::setStartposPercent(m_info.levelKey, m_index, v);
		});
		m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {45.f, -5.f});

		auto sign = CCLabelBMFont::create("%", "bigFont.fnt");
		sign->setScale(0.6f);
		m_mainLayer->addChildAtPosition(sign, Anchor::Center, {92.f, -5.f});

		// --- сброс на общий процент ---
		auto resetSpr = ButtonSprite::create("Use default", 120, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
		auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(StartposPopup::onReset));
		m_buttonMenu->addChildAtPosition(resetBtn, Anchor::Center, {0.f, -55.f});

		this->refresh();
		return true;
	}

	void refresh() {
		m_refreshing = true;

		std::string text = "Startpos " + std::to_string(m_index + 1) + "/" + std::to_string(m_info.count);
		if (m_index == m_info.current) text += " (now)";
		m_indexLabel->setString(text.c_str());

		m_input->setString(std::to_string(ad::deafenPercentFor(m_info.levelKey, m_index)));

		m_refreshing = false;
	}

	void onPrev(CCObject*) {
		m_index = (m_index - 1 + m_info.count) % m_info.count;
		this->refresh();
	}

	void onNext(CCObject*) {
		m_index = (m_index + 1) % m_info.count;
		this->refresh();
	}

	void onReset(CCObject*) {
		ad::setStartposPercent(m_info.levelKey, m_index, -1);
		this->refresh();
	}

public:
	static StartposPopup* create() {
		auto ret = new StartposPopup();
		if (ret->init()) {
			ret->autorelease();
			return ret;
		}
		CC_SAFE_DELETE(ret);
		return nullptr;
	}
};

// ============================================================
//  Главное окно настроек
// ============================================================
class AutoDeafenPopup : public Popup {
protected:
	enum class Capture { None, Discord, Toggle };

	ButtonSprite* m_discordSpr = nullptr;
	ButtonSprite* m_toggleSpr = nullptr;
	Capture m_capture = Capture::None;

	// Подпись слева (только латиница: шрифт chatFont не поддерживает кириллицу)
	void addLabel(char const* text, float y) {
		auto label = CCLabelBMFont::create(text, "chatFont.fnt");
		label->setAnchorPoint({0.f, 0.5f});
		label->setScale(0.9f);
		m_mainLayer->addChildAtPosition(label, Anchor::Center, {-88.f, y});
	}

	// Поле для ввода процента + знак %
	void addPercentInput(float y, std::string saveKey, int value) {
		auto input = TextInput::create(60.f, "0-100", "bigFont.fnt");
		input->setFilter("0123456789");
		input->setMaxCharCount(3);
		input->setString(std::to_string(value));
		input->setCallback([saveKey](std::string const& text) {
			if (text.empty()) return;
			int v = 0;
			try { v = std::stoi(text); } catch (...) { return; }
			v = std::clamp(v, 0, 100);
			Mod::get()->setSavedValue<int>(saveKey, v);
		});
		m_mainLayer->addChildAtPosition(input, Anchor::Center, {45.f, y});

		auto sign = CCLabelBMFont::create("%", "bigFont.fnt");
		sign->setScale(0.6f);
		m_mainLayer->addChildAtPosition(sign, Anchor::Center, {92.f, y});
	}

	CCMenuItemToggler* addToggler(float y, bool value, SEL_MenuHandler handler) {
		auto toggler = CCMenuItemToggler::createWithStandardSprites(this, handler, 0.7f);
		toggler->toggle(value);
		m_buttonMenu->addChildAtPosition(toggler, Anchor::Center, {-110.f, y});
		return toggler;
	}

	std::string toggleBindText() {
		if (m_capture == Capture::Toggle) return "Press a key...";
		return "Toggle: " + ad::bindToString(ad::getBind("toggle-"));
	}

	std::string discordBindText() {
		if (m_capture == Capture::Discord) return "Press a key...";
		return "Key: " + ad::bindToString(ad::getBind(""));
	}

	bool init() {
		if (!Popup::init(300.f, 310.f)) return false;
		this->setTitle("Mario Auto Deafen");
		this->setKeyboardEnabled(true);

		// --- Enabled ---
		this->addToggler(105.f, ad::enabled(), menu_selector(AutoDeafenPopup::onToggle));
		this->addLabel("Enabled", 105.f);

		// --- Клавиша вкл/выкл мода ---
		m_toggleSpr = ButtonSprite::create(
			this->toggleBindText().c_str(), 190, true, "bigFont.fnt", "GJ_button_04.png", 30.f, 0.6f
		);
		auto toggleBtn = CCMenuItemSpriteExtra::create(m_toggleSpr, this, menu_selector(AutoDeafenPopup::onToggleBindBtn));
		m_buttonMenu->addChildAtPosition(toggleBtn, Anchor::Center, {0.f, 70.f});

		// --- Клавиша Discord ---
		m_discordSpr = ButtonSprite::create(
			this->discordBindText().c_str(), 190, true, "bigFont.fnt", "GJ_button_04.png", 30.f, 0.6f
		);
		auto discordBtn = CCMenuItemSpriteExtra::create(m_discordSpr, this, menu_selector(AutoDeafenPopup::onDiscordBtn));
		m_buttonMenu->addChildAtPosition(discordBtn, Anchor::Center, {0.f, 36.f});

		// --- Процент, с которого включается deafen ---
		this->addLabel("Deafen at", -2.f);
		this->addPercentInput(-2.f, "percent", ad::percent());

		// --- Процент, на котором звук возвращается (по желанию) ---
		this->addToggler(-36.f, ad::undeafenEnabled(), menu_selector(AutoDeafenPopup::onToggleUndeafen));
		this->addLabel("Undeafen at", -36.f);
		this->addPercentInput(-36.f, "undeafen-percent", ad::undeafenPercent());

		// --- Работать со стартпосов ---
		this->addToggler(-70.f, ad::startposEnabled(), menu_selector(AutoDeafenPopup::onToggleStartpos));
		this->addLabel("Deafen on startpos", -70.f);

		// --- Нижний ряд кнопок ---
		auto spSpr = ButtonSprite::create("Startpos %", 110, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
		auto spBtn = CCMenuItemSpriteExtra::create(spSpr, this, menu_selector(AutoDeafenPopup::onStartposList));
		m_buttonMenu->addChildAtPosition(spBtn, Anchor::Center, {-65.f, -113.f});

		auto testSpr = ButtonSprite::create("Test key", 100, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
		auto testBtn = CCMenuItemSpriteExtra::create(testSpr, this, menu_selector(AutoDeafenPopup::onTest));
		m_buttonMenu->addChildAtPosition(testBtn, Anchor::Center, {70.f, -113.f});

		return true;
	}

	void refreshKeys() {
		m_toggleSpr->setString(this->toggleBindText().c_str());
		m_discordSpr->setString(this->discordBindText().c_str());
	}

	// toggler вызывает callback ДО смены состояния, поэтому значение инвертируем
	void onToggle(CCObject* sender) {
		auto t = static_cast<CCMenuItemToggler*>(sender);
		bool now = !t->isToggled();
		Mod::get()->setSavedValue<bool>("enabled", now);
		if (!now) ad::undeafen(); // выключили мод, пока звук отключён — возвращаем звук
		ad::refreshIndicator();
	}

	void onToggleUndeafen(CCObject* sender) {
		auto t = static_cast<CCMenuItemToggler*>(sender);
		Mod::get()->setSavedValue<bool>("undeafen-enabled", !t->isToggled());
	}

	void onToggleStartpos(CCObject* sender) {
		auto t = static_cast<CCMenuItemToggler*>(sender);
		Mod::get()->setSavedValue<bool>("startpos", !t->isToggled());
	}

	void onToggleBindBtn(CCObject*) {
		m_capture = (m_capture == Capture::Toggle) ? Capture::None : Capture::Toggle;
		this->refreshKeys();
	}

	void onDiscordBtn(CCObject*) {
		m_capture = (m_capture == Capture::Discord) ? Capture::None : Capture::Discord;
		this->refreshKeys();
	}

	void onStartposList(CCObject*) {
		StartposPopup::create()->show();
	}

	void onTest(CCObject*) {
		// Просто нажимает клавишу Discord один раз (для проверки, что Discord реагирует)
		ad::pressBind();
	}

	void handleKey(enumKeyCodes key) {
		if (m_capture == Capture::None) return;

		int vk = static_cast<int>(key);
		// одни модификаторы не считаем клавишей
		if (vk == 16 || vk == 17 || vk == 18 || (vk >= 160 && vk <= 165)) return;

		auto kb = CCKeyboardDispatcher::get();
		ad::Bind b;
		b.key = vk;
		b.ctrl = kb->getControlKeyPressed();
		b.shift = kb->getShiftKeyPressed();
		b.alt = kb->getAltKeyPressed();
		ad::setBind(m_capture == Capture::Toggle ? "toggle-" : "", b);

		m_capture = Capture::None;
		this->refreshKeys();
	}

public:
	// Две перегрузки без override: в разных версиях Geode/GD сигнатура keyDown
	// отличается (с параметром timestamp или без). Сработает та, что совпадает.
	void keyDown(enumKeyCodes key) { this->handleKey(key); }
	void keyDown(enumKeyCodes key, double) { this->handleKey(key); }

	static AutoDeafenPopup* create() {
		auto ret = new AutoDeafenPopup();
		if (ret->init()) {
			ret->autorelease();
			return ret;
		}
		CC_SAFE_DELETE(ret);
		return nullptr;
	}
};

// ============================================================
//  Иконка в меню паузы (сама ищет свободное место)
// ============================================================
class $modify(AutoDeafenPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto win = CCDirector::get()->getWinSize();

		// Надпись зелёная, если autodeafen включён, и серая, если выключен
		auto label = CCLabelBMFont::create("AD", "bigFont.fnt");
		label->setID("autodeafen-label"_spr);
		label->setColor(ad::enabled() ? ccColor3B{110, 255, 110} : ccColor3B{190, 190, 190});

		// Тёмно-фиолетовый кружок в стиле кнопок меню паузы, чуть меньше обычного
		auto spr = CircleButtonSprite::create(
			label,
			CircleBaseColor::DarkPurple,
			CircleBaseSize::Medium
		);
		spr->setScale(0.7f);

		auto btn = CCMenuItemSpriteExtra::create(
			spr, this, menu_selector(AutoDeafenPauseLayer::onAutoDeafen)
		);

		auto menu = CCMenu::create();
		menu->setID("autodeafen-menu"_spr);
		menu->setPosition({win.width - 37.f, win.height - 75.f});
		menu->addChild(btn);
		this->addChild(menu);

		// Кнопки других модов могут появиться позже — проверяем постоянно, пока открыта пауза
		this->updateButtonPos(0.f);
		this->schedule(schedule_selector(AutoDeafenPauseLayer::updateButtonPos), 0.3f);
	}

	void updateButtonPos(float) {
		auto menu = this->getChildByID("autodeafen-menu"_spr);
		if (menu) ad::placeButton(this, menu);
		ad::refreshIndicator();
	}

	void onAutoDeafen(CCObject*) {
		AutoDeafenPopup::create()->show();
	}
};

// ============================================================
//  Клавиша вкл/выкл мода (проверяется каждый кадр)
// ============================================================
class $modify(AutoDeafenScheduler, CCScheduler) {
	void update(float dt) {
		CCScheduler::update(dt);
		ad::pollToggleBind();
	}
};

// ============================================================
//  Хуки уровня
// ============================================================
class $modify(AutoDeafenPlayLayer, PlayLayer) {
	struct Fields {
		bool triggered = false;   // уже сработало в этой попытке
		bool logged = false;      // отладочная строка в лог уже записана
		int target = -1;          // процент deafen для этой попытки (-1 = ещё не посчитан)
		int frames = 0;           // сколько кадров прошло с начала попытки
		GameObject* spObj = nullptr; // стартпос, для которого запомнен процент ниже
		float spPct = 0.f;        // на каком проценте (как в игре) стоит этот стартпос
		CCLabelBMFont* pctLabel = nullptr; // надпись с процентами в игре
	};

	// Процент из надписи в игре (-1, если надписи нет)
	float shownPercent() {
		if (!m_fields->pctLabel) m_fields->pctLabel = ad::findPercentLabel();
		if (!m_fields->pctLabel) return -1.f;
		return ad::parsePercent(m_fields->pctLabel->getString());
	}

	// Процент прохождения от начала уровня — такой же, как показывает игра
	float absolutePercent() {
		float shown = this->shownPercent();
		if (shown >= 0.f) return shown;
		// запасной вариант, если надпись с процентами скрыта
		if (m_levelLength > 0.f && m_player1) {
			return std::clamp(m_player1->getPositionX() / m_levelLength * 100.f, 0.f, 100.f);
		}
		return this->getCurrentPercent();
	}

	// Примерный процент стартпоса по его позиции (используется как запасной)
	float startposPercent() {
		if (!m_startPosObject || m_levelLength <= 0.f) return 0.f;
		return std::clamp(m_startPosObject->getPositionX() / m_levelLength * 100.f, 0.f, 100.f);
	}

	void refreshTarget() {
		auto info = ad::startposInfo(this);
		int idx = m_startPosObject ? info.current : -1;
		m_fields->target = ad::deafenPercentFor(info.levelKey, idx);
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);

		m_fields->frames++;

		if (!ad::enabled()) return;

		bool fromStartpos = m_startPosObject != nullptr;
		if (fromStartpos) {
			// со стартпоса работаем, только если включено, и в любом режиме (в т.ч. практика)
			if (!ad::startposEnabled()) return;
		}
		else {
			if (m_isTestMode) return;
			if (m_isPracticeMode && !Mod::get()->getSettingValue<bool>("practice-mode")) return;
		}

		// первые кадры попытки пропускаем: игра ещё не обновила надпись с процентами
		if (m_fields->frames < 3) return;

		if (m_fields->target < 0) this->refreshTarget();

		float pct = this->absolutePercent();

		float sp = 0.f;
		if (fromStartpos) {
			// запоминаем процент стартпоса один раз для каждого стартпоса
			if (m_startPosObject != m_fields->spObj) {
				m_fields->spObj = m_startPosObject;
				m_fields->spPct = std::max(pct, this->startposPercent());
			}
			sp = m_fields->spPct;
		}

		if (!m_fields->logged) {
			m_fields->logged = true;
			log::info(
				"[AutoDeafen] attempt: startpos={}, startposPct={:.1f}, shownPct={:.1f}, calcPct={:.1f}, target={}",
				fromStartpos, sp, this->shownPercent(),
				m_levelLength > 0.f && m_player1 ? m_player1->getPositionX() / m_levelLength * 100.f : -1.f,
				m_fields->target
			);
		}

		// со стартпоса: указанный процент меньше процента самого стартпоса — ничего не делаем
		if (fromStartpos && m_fields->target < static_cast<int>(std::floor(sp))) return;

		if (!m_fields->triggered) {
			// включаем deafen, когда дошли до нужного процента
			if (pct >= static_cast<float>(m_fields->target)) {
				m_fields->triggered = true;
				ad::deafen();
				log::info("[AutoDeafen] DEAFEN at {:.1f}% (target {}%, startpos: {})", pct, m_fields->target, fromStartpos);
			}
		}
		else if (ad::g_deafened && ad::undeafenEnabled()
			&& ad::undeafenPercent() > m_fields->target
			&& pct >= static_cast<float>(ad::undeafenPercent())) {
			// возвращаем звук на втором проценте
			ad::undeafen();
			log::info("[AutoDeafen] UNDEAFEN at {:.1f}%", pct);
		}
	}

	void resetLevel() {
		ad::undeafen();
		m_fields->triggered = false;
		m_fields->logged = false;
		m_fields->target = -1;
		m_fields->frames = 0;
		m_fields->pctLabel = nullptr;
		PlayLayer::resetLevel();
	}

	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		PlayLayer::destroyPlayer(player, obj);
		if (!Mod::get()->getSettingValue<bool>("undeafen-on-death")) return;
		if (player && player->m_isDead) ad::undeafen();
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		if (Mod::get()->getSettingValue<bool>("undeafen-on-complete")) ad::undeafen();
	}

	void onQuit() {
		ad::undeafen();
		PlayLayer::onQuit();
	}
};
