#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <type_traits>

using namespace geode::prelude;

namespace {
constexpr char hudId[] = "pulsecounter.input-display/hud";
constexpr double referenceRate = 240.0;
constexpr std::array<ccColor3B, 7> colors{{
    {255, 85, 91}, {255, 145, 65}, {255, 215, 76}, {213, 249, 91},
    {120, 243, 112}, {112, 229, 255}, {178, 157, 255}
}};

void playDing() {
    const auto path = Mod::get()->getResourcesDir() / "ding.wav";
    // Geode 5 uses wide native paths on Windows, while Geode 6 supplies UTF-8.
    []<typename Character>(const Character* filename) {
        constexpr auto flags = SND_FILENAME | SND_ASYNC | SND_NODEFAULT;
        if constexpr (std::is_same_v<Character, wchar_t>) {
            PlaySoundW(filename, nullptr, flags);
        } else {
            auto wide = std::filesystem::u8path(filename).wstring();
            PlaySoundW(wide.c_str(), nullptr, flags);
        }
    }(path.c_str());
}

class CounterHUD : public CCNode {
public:
    std::array<std::optional<double>, 2> starts{};
    std::array<unsigned int, 7> totals{};
    std::array<CCLabelBMFont*, 7> labels{};

    static CounterHUD* attach(PlayLayer* play) {
        auto hud = new CounterHUD;
        if (!hud->init()) {
            delete hud;
            return nullptr;
        }
        hud->autorelease();
        hud->setID(hudId);
        play->addChild(hud, 1000);

        const auto size = CCDirector::sharedDirector()->getWinSize();
        auto title = CCLabelBMFont::create("Frame Counter", "goldFont.fnt");
        title->setAnchorPoint({0.f, 0.5f});
        title->setScale(0.42f);
        title->setPosition({12.f, size.height - 18.f});
        hud->addChild(title);

        for (size_t i = 0; i < hud->labels.size(); ++i) {
            auto label = CCLabelBMFont::create("", "bigFont.fnt");
            label->setAnchorPoint({0.f, 0.5f});
            label->setScale(0.38f);
            label->setColor(colors[i]);
            label->setPosition({14.f, size.height - 32.f - 12.f * static_cast<float>(i)});
            hud->addChild(label);
            hud->labels[i] = label;
            hud->updateRow(i);
        }
        return hud;
    }

    void updateRow(size_t bucket) {
        const auto text = fmt::format("{}{}: {}", bucket + 1,
            bucket == 6 ? "+" : "", totals[bucket]);
        labels[bucket]->setString(text.c_str());
    }

    void showMarker(PlayerObject* player, int frames) {
        auto parent = player->getParent();
        if (!parent) return;
        auto marker = CCNode::create();
        const auto world = parent->convertToWorldSpace(player->getPosition());
        marker->setPosition(convertToNodeSpace(world));
        addChild(marker);

        const auto color = frames ? colors[std::min(frames, 7) - 1] : ccColor3B{185, 253, 122};
        auto ring = CCDrawNode::create();
        constexpr int segments = 32;
        constexpr float step = 6.2831853f / segments;
        auto point = [](int index) {
            const auto angle = index * step;
            return CCPoint{13.f * std::cos(angle), 13.f * std::sin(angle)};
        };
        for (int i = 0; i < segments; ++i) {
            ring->drawSegment(point(i), point(i + 1), 1.f,
                ccc4f(color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f));
        }
        marker->addChild(ring);
        if (frames) {
            auto text = fmt::format("{}", frames);
            auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            label->setScale(0.42f);
            label->setPosition({0.f, 20.f});
            marker->addChild(label);
        }
        marker->runAction(CCSequence::create(
            CCDelayTime::create(0.24f), CCRemoveSelf::create(true), nullptr));
    }
};
}

class $modify(PulsePlay, PlayLayer) {
    void resetLevel() {
        removeChildByID(hudId);
        PlayLayer::resetLevel();
    }
};

class $modify(PulsePlayer, PlayerObject) {
    void recordJump(PlayerButton button, bool down) {
        if (static_cast<int>(button) != 1) return;
        auto play = PlayLayer::get();
        if (!play || m_isDead || (this != play->m_player1 && this != play->m_player2)) return;
        auto hud = static_cast<CounterHUD*>(play->getChildByID(hudId));
        if (!hud && down) hud = CounterHUD::attach(play);
        if (!hud) return;

        auto& start = hud->starts[this == play->m_player1 ? 0 : 1];
        const double time = play->m_gameState.m_levelTime;
        if (!std::isfinite(time)) return;
        if (start && time < *start) start.reset();
        if (down) {
            if (!start) {
                start = time;
                hud->showMarker(this, 0);
            }
            return;
        }
        if (!start) return;
        const int frames = std::max(1, static_cast<int>(std::lround((time - *start) * referenceRate)));
        start.reset();
        const auto bucket = std::min(frames, 7) - 1;
        ++hud->totals[bucket];
        hud->updateRow(bucket);
        if (Mod::get()->getSettingValue<bool>("release-markers")) hud->showMarker(this, frames);
        if (frames == 1 && Mod::get()->getSettingValue<bool>("sound")) playDing();
    }

    bool pushButton(PlayerButton button) {
        recordJump(button, true);
        return PlayerObject::pushButton(button);
    }

    bool releaseButton(PlayerButton button) {
        recordJump(button, false);
        return PlayerObject::releaseButton(button);
    }
};
