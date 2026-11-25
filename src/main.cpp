#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

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

class CounterHUD : public CCNode {
public:
    CCLabelBMFont* status = nullptr;
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
        hud->status = CCLabelBMFont::create("", "bigFont.fnt");
        hud->status->setAnchorPoint({0.f, 0.5f});
        hud->status->setScale(0.33f);
        hud->status->setPosition({12.f, size.height - 122.f});
        hud->addChild(hud->status);
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


namespace {
enum class Phase { Recording, Ready, Baseline, Scanning, Playback, Finished };

struct Input {
    int tick;
    bool down;
    int button;
    bool playerFlag;
    int step;
    std::optional<CCPoint> position;
};

struct Jump {
    size_t press;
    std::optional<size_t> release;
    int width = 1;
};

struct Analysis {
    Phase phase = Phase::Recording;
    std::vector<Input> recorded;
    std::vector<Input> replay;
    std::vector<Jump> jumps;
    std::array<std::optional<size_t>, 2> openJump{};
    size_t nextInput = 0;
    size_t currentJump = 0;
    size_t nextMarker = 0;
    int shift = -1;
    int endTick = 0;
    size_t verifiedTiming = 0;
    int failureTick = 0;
    bool internalReset = false;
    std::optional<bool> outcome;

    bool replaying() const {
        return phase == Phase::Baseline || phase == Phase::Scanning || phase == Phase::Playback;
    }

    void record(Input input) {
        if (input.button == 1) {
            auto& open = openJump[input.playerFlag ? 1 : 0];
            if (input.down && !open) {
                open = jumps.size();
                jumps.push_back({recorded.size(), std::nullopt});
            } else if (!input.down && open) {
                jumps[*open].release = recorded.size();
                open.reset();
            }
        }
        recorded.push_back(input);
    }

    bool prepareShift() {
        const auto& jump = jumps[currentJump];
        const auto pressTick = recorded[jump.press].tick + shift;
        if (pressTick < 0) return false;

        // Preserve the jump's input shape. Only its position on the tick grid
        // changes, and all other jumps retain their recorded inputs.
        for (size_t i = 0; i < recorded.size(); ++i) {
            if (i == jump.press || (jump.release && i == *jump.release)) continue;
            const auto& other = recorded[i];
            const auto& press = recorded[jump.press];
            if (other.button != press.button || other.playerFlag != press.playerFlag) continue;

            if (i < jump.press && other.tick > pressTick) return false;
            if (!jump.release && i > jump.press && other.tick < pressTick) return false;
            if (jump.release && i > *jump.release &&
                other.tick < recorded[*jump.release].tick + shift) return false;
        }

        replay = recorded;
        replay[jump.press].tick = pressTick;
        if (jump.release) replay[*jump.release].tick += shift;
        std::stable_sort(replay.begin(), replay.end(), [](const Input& a, const Input& b) {
            return a.tick < b.tick;
        });
        return true;
    }

    void advance(bool success) {
        auto& jump = jumps[currentJump];
        if (success) {
            ++jump.width;
            if (jump.width < 7) {
                shift += shift < 0 ? -1 : 1;
                return;
            }
        } else if (shift < 0) {
            shift = 1;
            return;
        }
        ++currentJump;
        shift = -1;
    }
};

struct ReplayTiming {
    bool beforeCommands;
    int offset;
    bool restoreStep;
};

constexpr std::array<ReplayTiming, 8> timings{{
    {true, 0, true}, {false, 0, true},
    {true, -1, true}, {false, -1, true},
    {true, 1, true}, {false, 1, true},
    {true, 0, false}, {false, 0, false}
}};

int currentTick(GJBaseGameLayer* layer) {
    return static_cast<int>(std::floor(layer->m_gameState.m_levelTime * referenceRate + 0.00001));
}
}

class $modify(PulsePlay, PlayLayer) {
    struct Fields {
        Analysis analysis;
    };

    CounterHUD* hud() {
        if (auto existing = getChildByID(hudId)) return static_cast<CounterHUD*>(existing);
        return CounterHUD::attach(this);
    }

    void restartReplay() {
        auto& a = m_fields->analysis;
        a.nextInput = 0;
        a.nextMarker = 0;
        a.outcome.reset();
        a.internalReset = true;
        resetLevelFromStart();
        a.internalReset = false;
    }

    void resetLevel() {
        auto& a = m_fields->analysis;
        if (a.internalReset) {
            PlayLayer::resetLevel();
            return;
        }
        const bool analyze = a.phase == Phase::Ready && !m_isPracticeMode;
        removeChildByID(hudId);
        if (analyze) {
            a.phase = Phase::Baseline;
            a.replay = a.recorded;
        } else {
            a = Analysis{};
        }
        PlayLayer::resetLevel();
        if (analyze) {
            if (auto panel = hud()) panel->status->setString("Checking original replay...");
        }
    }

    void levelComplete() {
        auto& a = m_fields->analysis;
        if (a.replaying()) {
            if (!a.outcome) {
                const bool noMissedPress = std::none_of(a.replay.begin() + a.nextInput,
                    a.replay.end(), [](const Input& input) { return input.down; });
                const bool matchingFinish = a.phase == Phase::Scanning ||
                    std::abs(currentTick(this) - a.endTick) <= 2;
                a.outcome = noMissedPress && matchingFinish;
                a.failureTick = currentTick(this);
            }
            return;
        }
        if (a.phase == Phase::Recording && !m_isPracticeMode && !a.jumps.empty()) {
            a.endTick = currentTick(this);
            a.phase = Phase::Ready;
            Notification::create("Replay the level to measure jump windows.", NotificationIcon::Info)->show();
        }
        PlayLayer::levelComplete();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& a = m_fields->analysis;
        if (a.replaying() && object != m_anticheatSpike) {
            if (!a.outcome) {
                a.outcome = false;
                a.failureTick = currentTick(this);
            }
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& a = m_fields->analysis;
        if (!a.replaying()) return;
        if (currentTick(this) > a.endTick + 480 && !a.outcome) {
            a.outcome = false;
            a.failureTick = currentTick(this);
        }
        if (!a.outcome) return;
        const bool passed = *a.outcome;
        if (a.phase == Phase::Baseline && !passed && a.verifiedTiming + 1 < timings.size()) {
            ++a.verifiedTiming;
            if (auto panel = hud()) {
                const auto progress = fmt::format("Checking replay ({}/{})...",
                    a.verifiedTiming + 1, timings.size());
                panel->status->setString(progress.c_str());
            }
            restartReplay();
            return;
        }
        if (a.phase == Phase::Playback || (a.phase == Phase::Baseline && !passed)) {
            const bool wasPlayback = a.phase == Phase::Playback;
            a.phase = Phase::Finished;
            restartReplay();
            if (auto panel = hud()) {
                const auto status = wasPlayback && passed ? std::string("Analysis complete")
                    : fmt::format("Replay desynced at tick {}", a.failureTick);
                panel->status->setString(status.c_str());
            }
            return;
        }
        if (a.phase == Phase::Baseline) {
            a.phase = Phase::Scanning;
        } else {
            a.advance(passed);
        }

        while (a.currentJump < a.jumps.size() && !a.prepareShift()) a.advance(false);
        if (a.currentJump == a.jumps.size()) {
            a.phase = Phase::Playback;
            a.replay = a.recorded;
            if (auto panel = hud()) panel->status->setString("Showing measured windows...");
        } else if (auto panel = hud()) {
            const auto progress = fmt::format("Jump {}/{} ({:+} ticks)",
                a.currentJump + 1, a.jumps.size(), a.shift);
            panel->status->setString(progress.c_str());
        }
        restartReplay();
    }

    void showJump(const Jump& jump) {
        auto panel = hud();
        if (!panel) return;
        const auto& input = m_fields->analysis.recorded[jump.press];
        const auto bucket = jump.width - 1;
        ++panel->totals[bucket];
        panel->updateRow(bucket);
        if (jump.width < 7 && input.position && m_objectLayer &&
            Mod::get()->getSettingValue<bool>("release-markers")) {
            panel->showMarker(m_objectLayer, *input.position, jump.width);
        }
        if (jump.width == 1 && Mod::get()->getSettingValue<bool>("sound")) playDing();
    }
};

class $modify(PulseCommands, GJBaseGameLayer) {
    PulsePlay* pulsePlay() {
        auto play = PlayLayer::get();
        return static_cast<GJBaseGameLayer*>(play) == this
            ? static_cast<PulsePlay*>(play) : nullptr;
    }

    void handleButton(bool down, int button, bool playerFlag) {
        auto play = pulsePlay();
        if (play) {
            auto& a = play->m_fields->analysis;
            if (a.replaying() || a.internalReset) return;
            if (a.phase == Phase::Recording && !play->m_isPracticeMode &&
                !m_levelEndAnimationStarted && m_player1 && !m_player1->m_isDead) {
                Input input{currentTick(this), down, button, playerFlag, m_currentStep, std::nullopt};
                auto player = m_gameState.m_isDualMode && !playerFlag ? m_player2 : m_player1;
                if (button == 1 && down && player && player->getParent() && m_objectLayer) {
                    const auto world = player->getParent()->convertToWorldSpace(player->getPosition());
                    input.position = m_objectLayer->convertToNodeSpace(world);
                }
                a.record(input);
            }
        }
        GJBaseGameLayer::handleButton(down, button, playerFlag);
    }

    void injectReplay(bool beforeCommands) {
        auto play = pulsePlay();
        if (!play) return;
        auto& a = play->m_fields->analysis;
        if (!a.replaying() || a.internalReset || a.outcome) return;
        const auto timing = timings[a.verifiedTiming];
        if (timing.beforeCommands != beforeCommands) return;
        const auto tick = currentTick(this) - timing.offset;
        while (a.nextInput < a.replay.size() && a.replay[a.nextInput].tick <= tick) {
            const auto& input = a.replay[a.nextInput++];
            const auto savedStep = m_currentStep;
            if (timing.restoreStep) m_currentStep = input.step;
            GJBaseGameLayer::handleButton(input.down, input.button, input.playerFlag);
            if (timing.restoreStep) m_currentStep = savedStep;
        }
        if (a.phase != Phase::Playback) return;
        while (a.nextMarker < a.jumps.size() &&
            a.recorded[a.jumps[a.nextMarker].press].tick <= tick) {
            play->showJump(a.jumps[a.nextMarker++]);
        }
    }
    void processCommands(float dt, bool halfTick, bool lastTick) {
        injectReplay(true);
        GJBaseGameLayer::processCommands(dt, halfTick, lastTick);
        injectReplay(false);
    }
};
