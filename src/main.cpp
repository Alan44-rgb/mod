#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

using namespace geode::prelude;

namespace {
constexpr char hudId[] = "pulsecounter.input-display/hud";
constexpr double referenceRate = 240.0;
constexpr std::array<ccColor3B, 7> colors{{
    {255, 85, 91}, {255, 145, 65}, {255, 215, 76}, {213, 249, 91},
    {120, 243, 112}, {112, 229, 255}, {178, 157, 255}
}};

void playDing() {
    // Play directly from persistent PCM memory so packaging and resource paths
    // cannot prevent the sound from loading.
    static const auto wav = [] {
        constexpr int sampleRate = 22050;
        constexpr int samples = 3200;
        std::array<char, 44 + samples * 2> bytes{};
        const auto put = [&](size_t offset, uint32_t value, size_t width) {
            for (size_t i = 0; i < width; ++i) {
                bytes[offset + i] = static_cast<char>((value >> (8 * i)) & 0xff);
            }
        };
        put(0, 0x46464952, 4);  // RIFF
        put(4, 36 + samples * 2, 4);
        put(8, 0x45564157, 4);  // WAVE
        put(12, 0x20746d66, 4); // fmt
        put(16, 16, 4);
        put(20, 1, 2);
        put(22, 1, 2);
        put(24, sampleRate, 4);
        put(28, sampleRate * 2, 4);
        put(32, 2, 2);
        put(34, 16, 2);
        put(36, 0x61746164, 4); // data
        put(40, samples * 2, 4);
        for (int i = 0; i < samples; ++i) {
            const auto t = static_cast<double>(i) / sampleRate;
            const auto envelope = std::min(1., t / 0.003) * std::exp(-31. * t);
            const auto sample = static_cast<int16_t>(19000. * envelope *
                (std::sin(8293.8 * t) + 0.25 * std::sin(12440.7 * t)));
            put(44 + 2 * i, static_cast<uint16_t>(sample), 2);
        }
        return bytes;
    }();
    if (!PlaySoundA(wav.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
        geode::log::warn("Pulse Counter could not play the ding.");
    }
}

struct JumpPress {
    double time;
    std::optional<CCPoint> position;
};

class CounterHUD : public CCNode {
public:
    std::array<std::optional<JumpPress>, 2> starts{};
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
        auto title = CCLabelBMFont::create("Hold Counter", "goldFont.fnt");
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

    void showMarker(CCNode* levelLayer, CCPoint position, int frames) {
        auto marker = CCNode::create();
        marker->setPosition(position);
        marker->setID("pulsecounter.input-display/ring");
        levelLayer->addChild(marker, 100);

        const auto color = colors[frames - 1];
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
        auto text = fmt::format("{}", frames);
        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setScale(0.42f);
        label->setPosition({0.f, 20.f});
        marker->addChild(label);
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
        if (start && time < start->time) start.reset();
        if (down) {
            if (!start) {
                start = JumpPress{time, std::nullopt};
                if (auto parent = getParent(); parent && play->m_objectLayer) {
                    const auto world = parent->convertToWorldSpace(getPosition());
                    start->position = play->m_objectLayer->convertToNodeSpace(world);
                }
            }
            return;
        }
        if (!start) return;
        const int frames = std::max(1, static_cast<int>(std::lround((time - start->time) * referenceRate)));
        const auto position = start->position;
        start.reset();
        const auto bucket = std::min(frames, 7) - 1;
        ++hud->totals[bucket];
        hud->updateRow(bucket);
        if (frames < 7 && position && play->m_objectLayer &&
            Mod::get()->getSettingValue<bool>("release-markers")) {
            hud->showMarker(play->m_objectLayer, *position, frames);
        }
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
