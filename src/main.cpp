#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

#include <thread>
#include <vector>
#include <algorithm>

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

	inline bool enabled() { return Mod::get()->getSavedValue<bool>("enabled", false); }
	inline int percent() { return Mod::get()->getSavedValue<int>("percent", 50); }
	inline bool undeafenEnabled() { return Mod::get()->getSavedValue<bool>("undeafen-enabled", false); }
	inline int undeafenPercent() { return Mod::get()->getSavedValue<int>("undeafen-percent", 80); }

	inline Bind getBind() {
		auto m = Mod::get();
		Bind b;
		b.key = m->getSavedValue<int>("key", 0);
		b.ctrl = m->getSavedValue<bool>("ctrl", false);
		b.shift = m->getSavedValue<bool>("shift", false);
		b.alt = m->getSavedValue<bool>("alt", false);
		return b;
	}

	inline void setBind(Bind const& b) {
		auto m = Mod::get();
		m->setSavedValue<int>("key", b.key);
		m->setSavedValue<bool>("ctrl", b.ctrl);
		m->setSavedValue<bool>("shift", b.shift);
		m->setSavedValue<bool>("alt", b.alt);
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

	// Нажимает сохранённую комбинацию (глобально, Discord её поймает как хоткей)
	inline void pressBind() {
#ifdef GEODE_IS_WINDOWS
		auto b = getBind();
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
}

// ============================================================
//  Попап настроек
// ============================================================
class AutoDeafenPopup : public Popup {
protected:
	ButtonSprite* m_keySpr = nullptr;
	bool m_capturing = false;

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

	bool init() {
		if (!Popup::init(300.f, 270.f)) return false;
		this->setTitle("Mario Auto Deafen");
		this->setKeyboardEnabled(true);

		// --- Enabled ---
		auto toggler = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(AutoDeafenPopup::onToggle), 0.7f
		);
		toggler->toggle(ad::enabled());
		m_buttonMenu->addChildAtPosition(toggler, Anchor::Center, {-110.f, 75.f});
		this->addLabel("Enabled", 75.f);

		// --- Клавиша ---
		m_keySpr = ButtonSprite::create(
			("Key: " + ad::bindToString(ad::getBind())).c_str(),
			190, true, "bigFont.fnt", "GJ_button_04.png", 30.f, 0.6f
		);
		auto keyBtn = CCMenuItemSpriteExtra::create(m_keySpr, this, menu_selector(AutoDeafenPopup::onKeyBtn));
		m_buttonMenu->addChildAtPosition(keyBtn, Anchor::Center, {0.f, 35.f});

		// --- Процент, с которого включается deafen ---
		this->addLabel("Deafen at", -5.f);
		this->addPercentInput(-5.f, "percent", ad::percent());

		// --- Процент, на котором звук возвращается (по желанию) ---
		auto undeafenToggler = CCMenuItemToggler::createWithStandardSprites(
			this, menu_selector(AutoDeafenPopup::onToggleUndeafen), 0.7f
		);
		undeafenToggler->toggle(ad::undeafenEnabled());
		m_buttonMenu->addChildAtPosition(undeafenToggler, Anchor::Center, {-110.f, -45.f});
		this->addLabel("Undeafen at", -45.f);
		this->addPercentInput(-45.f, "undeafen-percent", ad::undeafenPercent());

		// --- Тест ---
		auto testSpr = ButtonSprite::create("Test key", 100, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.6f);
		auto testBtn = CCMenuItemSpriteExtra::create(testSpr, this, menu_selector(AutoDeafenPopup::onTest));
		m_buttonMenu->addChildAtPosition(testBtn, Anchor::Center, {0.f, -95.f});

		return true;
	}

	void refreshKey() {
		if (m_capturing) m_keySpr->setString("Press a key...");
		else m_keySpr->setString(("Key: " + ad::bindToString(ad::getBind())).c_str());
	}

	void onToggle(CCObject* sender) {
		// toggler вызывает callback ДО смены состояния, поэтому инвертируем
		auto t = static_cast<CCMenuItemToggler*>(sender);
		Mod::get()->setSavedValue<bool>("enabled", !t->isToggled());
	}

	void onToggleUndeafen(CCObject* sender) {
		auto t = static_cast<CCMenuItemToggler*>(sender);
		Mod::get()->setSavedValue<bool>("undeafen-enabled", !t->isToggled());
	}

	void onKeyBtn(CCObject*) {
		m_capturing = !m_capturing;
		this->refreshKey();
	}

	void onTest(CCObject*) {
		// Просто нажимает клавишу один раз (для проверки, что Discord реагирует)
		ad::pressBind();
	}

	void handleKey(enumKeyCodes key) {
		if (!m_capturing) return;

		int vk = static_cast<int>(key);
		// одни модификаторы не считаем клавишей
		if (vk == 16 || vk == 17 || vk == 18 || (vk >= 160 && vk <= 165)) return;

		auto kb = CCKeyboardDispatcher::get();
		ad::Bind b;
		b.key = vk;
		b.ctrl = kb->getControlKeyPressed();
		b.shift = kb->getShiftKeyPressed();
		b.alt = kb->getAltKeyPressed();
		ad::setBind(b);

		m_capturing = false;
		this->refreshKey();
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
//  Иконка в меню паузы (справа сверху)
// ============================================================
class $modify(AutoDeafenPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto win = CCDirector::get()->getWinSize();

		// Надпись зелёная, если autodeafen включён, и серая, если выключен
		auto label = CCLabelBMFont::create("AD", "bigFont.fnt");
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
		// Справа сверху, ПОД шестерёнкой настроек
		menu->setPosition({win.width - 37.f, win.height - 72.f});
		menu->addChild(btn);
		this->addChild(menu);
	}

	void onAutoDeafen(CCObject*) {
		AutoDeafenPopup::create()->show();
	}
};

// ============================================================
//  Хуки уровня
// ============================================================
class $modify(AutoDeafenPlayLayer, PlayLayer) {
	struct Fields {
		bool triggered = false; // уже сработало в этой попытке
	};

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);

		if (!ad::enabled()) return;
		if (m_isTestMode) return;
		if (m_isPracticeMode && !Mod::get()->getSettingValue<bool>("practice-mode")) return;

		float pct = this->getCurrentPercent();

		if (!m_fields->triggered) {
			// включаем deafen, когда дошли до нужного процента
			if (pct >= static_cast<float>(ad::percent())) {
				m_fields->triggered = true;
				ad::deafen();
			}
		}
		else if (ad::g_deafened && ad::undeafenEnabled()
			&& ad::undeafenPercent() > ad::percent()
			&& pct >= static_cast<float>(ad::undeafenPercent())) {
			// возвращаем звук на втором проценте
			ad::undeafen();
		}
	}

	void resetLevel() {
		ad::undeafen();
		m_fields->triggered = false;
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
