#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <Windows.h>
#include <mmsystem.h>
#include <array>
#include <cmath>
#include <optional>

using namespace geode::prelude;

namespace {
void showMarker(PlayerObject* player, bool pressed, std::optional<double> duration) {
    auto parent = player->getParent();
    if (!parent) return;

    auto marker = CCNode::create();
    marker->setPosition(player->getPosition());
    parent->addChild(marker, player->getZOrder() + 1);

    const bool shortHold = duration && *duration <= Mod::get()->getSettingValue<int64_t>("ding-ms");
    auto color = pressed ? ccc4f(0.62f, 1.f, 0.27f, 1.f)
                         : shortHold ? ccc4f(1.f, 0.45f, 0.27f, 1.f)
                                     : ccc4f(0.38f, 0.86f, 1.f, 1.f);

    auto ring = CCDrawNode::create();
    // A ring composed of segments works with the cocos2d version shipped by GD.
    constexpr int segments = 32;
    for (int i = 0; i < segments; ++i) {
        auto point = [](int index) {
            const auto angle = static_cast<float>(index) * 6.2831853f / segments;
            return CCPoint{19.f * std::cos(angle), 19.f * std::sin(angle)};
        };
        ring->drawSegment(point(i), point(i + 1), 1.15f, color);
    }
    marker->addChild(ring);

    if (duration) {
        auto text = fmt::format("{:.1f} ms", *duration);
        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setScale(0.35f);
        label->setPosition({0.f, 30.f});
        marker->addChild(label);
    }

    marker->runAction(CCSequence::create(
        CCDelayTime::create(0.24f), CCRemoveSelf::create(true), nullptr
    ));

    if (shortHold && Mod::get()->getSettingValue<bool>("sound")) {
        const auto path = Mod::get()->getResourcesDir() / "ding.wav";
        PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    }
}
}

class $modify(PulseInput, GJBaseGameLayer) {
    struct Fields {
        std::array<std::optional<double>, 2> pressedAt;
    };

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (button != 1 || PlayLayer::get() != this) return;

        auto player = isPlayer1 ? m_player1 : m_player2;
        if (!player || player->m_isDead) return;

        auto& start = m_fields->pressedAt[isPlayer1 ? 0 : 1];
        const double time = m_gameState.m_levelTime;
        if (start && time < *start) start.reset();
        if (down) {
            if (start) return;
            start = time;
            showMarker(player, true, std::nullopt);
            return;
        }
        if (!start) return;
        const double milliseconds = (time - *start) * 1000.;
        start.reset();
        if (Mod::get()->getSettingValue<bool>("release-markers")) {
            showMarker(player, false, milliseconds);
        }
    }
};
