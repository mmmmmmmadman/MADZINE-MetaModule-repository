#include "plugin.hpp"
#include "WorldRhythm/PatternGenerator.hpp"
#include "WorldRhythm/HumanizeEngine.hpp"
#include "WorldRhythm/StyleProfiles.hpp"
#include "WorldRhythm/MinimalDrumSynth.hpp"
#include "WorldRhythm/RestEngine.hpp"
#include "WorldRhythm/FillGenerator.hpp"
#include "WorldRhythm/ArticulationEngine.hpp"
#include "WorldRhythm/ArticulationProfiles.hpp"
#include "WorldRhythm/KotekanEngine.hpp"
#include "WorldRhythm/LlamadaEngine.hpp"
#include "WorldRhythm/CrossRhythmEngine.hpp"
#include "WorldRhythm/AsymmetricGroupingEngine.hpp"
#include "WorldRhythm/AmenBreakEngine.hpp"

// ============================================================================
// Uni Rhythm Module - 32HP (MetaModule port)
// Cross-cultural rhythm generator with integrated synthesis
// Per-role Style/Density/Length controls
// Global REST parameter with RestEngine
// Master Isolator + Tube Drive
// ============================================================================

// ============================================================================
// Style names and colors
// ============================================================================

using namespace worldrhythm;

namespace unirhythm {

static const char* UR_STYLE_NAMES[10] = {
    "W.African", "Afro-Cuban", "Brazilian", "Balkan", "Indian",
    "Gamelan", "Jazz", "Electronic", "Breakbeat", "Techno"
};

// ============================================================================
// Custom ParamQuantity for Style with names
// ============================================================================

struct URStyleParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int index = static_cast<int>(getValue());
        if (index >= 0 && index < 10) {
            return UR_STYLE_NAMES[index];
        }
        return ParamQuantity::getDisplayValueString();
    }
};

// ============================================================================
// Extended Drum Synth - 8 voices (UniRhythm namespace)
// ============================================================================

class URExtendedDrumSynth {
private:
    MinimalVoice voices[8];
    float sampleRate = 44100.0f;

public:
    void setSampleRate(float sr) {
        sampleRate = sr;
        for (int i = 0; i < 8; i++) {
            voices[i].setSampleRate(sr);
        }
    }

    void setVoiceParams(int voice, SynthMode mode, float freq, float decay, float sweep = 0.f, float bend = 1.f) {
        if (voice < 0 || voice > 7) return;
        voices[voice].setMode(mode);
        voices[voice].setFreq(freq);
        voices[voice].setDecay(decay);
        // sweep/bend not supported on MetaModule MinimalVoice
    }

    void triggerVoice(int voice, float velocity = 1.0f) {
        if (voice < 0 || voice > 7) return;
        voices[voice].trigger(velocity);
    }

    float processVoice(int voice) {
        if (voice < 0 || voice > 7) return 0.0f;
        return voices[voice].process();
    }
};

// 8-voice style presets
struct URExtendedStylePreset {
    struct VoicePreset {
        SynthMode mode;
        float freq;
        float decay;
        const char* name;
        float sweep = 0.f;
        float bend = 1.f;
    };
    VoicePreset voices[8];
};

// Voice assignments per style (2 voices per role):
// 0-1: Timeline, 2-3: Foundation, 4-5: Groove, 6-7: Lead

static const URExtendedStylePreset UR_EXTENDED_PRESETS[10] = {
    // 0: West African
    {{{SynthMode::SINE, 4500.0f, 60.0f, "Gankogui"},
      {SynthMode::SINE, 3500.0f, 40.0f, "Bell Lo"},
      {SynthMode::SINE, 80.0f, 200.0f, "Dununba"},
      {SynthMode::SINE, 120.0f, 150.0f, "Dunun"},
      {SynthMode::SINE, 250.0f, 80.0f, "Sangban"},
      {SynthMode::SINE, 300.0f, 60.0f, "Kenkeni"},
      {SynthMode::NOISE, 700.0f, 40.0f, "Djembe Slap"},
      {SynthMode::NOISE, 400.0f, 50.0f, "Djembe Tone"}}},

    // 1: Afro-Cuban
    {{{SynthMode::SINE, 4000.0f, 20.0f, "Clave"},
      {SynthMode::SINE, 2000.0f, 30.0f, "Cowbell"},
      {SynthMode::SINE, 100.0f, 150.0f, "Tumba"},
      {SynthMode::SINE, 150.0f, 120.0f, "Conga Lo"},
      {SynthMode::SINE, 350.0f, 70.0f, "Conga Mid"},
      {SynthMode::SINE, 550.0f, 50.0f, "Quinto"},
      {SynthMode::NOISE, 3000.0f, 40.0f, "Timbales"},
      {SynthMode::NOISE, 5000.0f, 25.0f, "Quinto Slap"}}},

    // 2: Brazilian
    {{{SynthMode::SINE, 4500.0f, 35.0f, "Agogo Hi"},
      {SynthMode::SINE, 3000.0f, 35.0f, "Agogo Lo"},
      {SynthMode::SINE, 55.0f, 250.0f, "Surdo"},
      {SynthMode::SINE, 80.0f, 180.0f, "Surdo 2"},
      {SynthMode::SINE, 400.0f, 40.0f, "Tamborim"},
      {SynthMode::NOISE, 500.0f, 50.0f, "Caixa"},
      {SynthMode::NOISE, 6000.0f, 30.0f, "Ganza"},
      {SynthMode::NOISE, 8000.0f, 20.0f, "Chocalho"}}},

    // 3: Balkan
    {{{SynthMode::NOISE, 4000.0f, 25.0f, "Rim"},
      {SynthMode::NOISE, 3500.0f, 15.0f, "Click"},
      {SynthMode::SINE, 90.0f, 180.0f, "Tapan Bass"},
      {SynthMode::SINE, 130.0f, 120.0f, "Tapan Mid"},
      {SynthMode::SINE, 300.0f, 50.0f, "Tarabuka Doum"},
      {SynthMode::SINE, 450.0f, 35.0f, "Tarabuka Tek"},
      {SynthMode::NOISE, 3000.0f, 25.0f, "Tek Hi"},
      {SynthMode::NOISE, 5000.0f, 20.0f, "Ka"}}},

    // 4: Indian
    {{{SynthMode::NOISE, 8000.0f, 150.0f, "Manjira"},
      {SynthMode::NOISE, 6000.0f, 100.0f, "Ghungroo"},
      {SynthMode::SINE, 65.0f, 300.0f, "Baya Ge"},
      {SynthMode::SINE, 90.0f, 200.0f, "Baya Ka"},
      {SynthMode::SINE, 350.0f, 100.0f, "Daya Na"},
      {SynthMode::SINE, 500.0f, 80.0f, "Daya Tin"},
      {SynthMode::NOISE, 1500.0f, 60.0f, "Daya Ti"},
      {SynthMode::NOISE, 2500.0f, 40.0f, "Daya Re"}}},

    // 5: Gamelan
    {{{SynthMode::SINE, 700.0f, 400.0f, "Kenong"},
      {SynthMode::SINE, 600.0f, 350.0f, "Kethuk"},
      {SynthMode::SINE, 90.0f, 800.0f, "Gong"},
      {SynthMode::SINE, 150.0f, 500.0f, "Kempul"},
      {SynthMode::SINE, 800.0f, 200.0f, "Bonang Po"},
      {SynthMode::SINE, 1000.0f, 180.0f, "Bonang Sa"},
      {SynthMode::SINE, 1200.0f, 250.0f, "Gender"},
      {SynthMode::SINE, 1400.0f, 220.0f, "Saron"}}},

    // 6: Jazz
    {{{SynthMode::NOISE, 4500.0f, 120.0f, "Ride"},
      {SynthMode::NOISE, 2500.0f, 80.0f, "Ride Bell"},
      {SynthMode::SINE, 50.0f, 200.0f, "Kick"},
      {SynthMode::SINE, 80.0f, 150.0f, "Kick Ghost"},
      {SynthMode::NOISE, 500.0f, 100.0f, "Snare"},
      {SynthMode::NOISE, 400.0f, 60.0f, "Snare Ghost"},
      {SynthMode::NOISE, 8000.0f, 35.0f, "HiHat Cl"},
      {SynthMode::NOISE, 6000.0f, 150.0f, "HiHat Op"}}},

    // 7: Electronic
    {{{SynthMode::NOISE, 9000.0f, 30.0f, "HiHat"},
      {SynthMode::NOISE, 12000.0f, 20.0f, "HiHat Ac"},
      {SynthMode::SINE, 45.0f, 280.0f, "808 Kick", 120.f, 0.8f},
      {SynthMode::SINE, 60.0f, 200.0f, "Kick 2", 80.f, 1.0f},
      {SynthMode::NOISE, 1500.0f, 70.0f, "Clap"},
      {SynthMode::NOISE, 2500.0f, 50.0f, "Snare"},
      {SynthMode::NOISE, 6000.0f, 150.0f, "Open HH"},
      {SynthMode::SINE, 800.0f, 100.0f, "Perc"}}},

    // 8: Breakbeat
    {{{SynthMode::NOISE, 8000.0f, 25.0f, "HiHat"},
      {SynthMode::NOISE, 10000.0f, 15.0f, "HiHat Ac"},
      {SynthMode::SINE, 55.0f, 180.0f, "Kick", 140.f, 1.0f},
      {SynthMode::SINE, 70.0f, 120.0f, "Kick Gho", 60.f, 1.2f},
      {SynthMode::NOISE, 2500.0f, 80.0f, "Snare"},
      {SynthMode::NOISE, 2000.0f, 50.0f, "Snare Gh"},
      {SynthMode::NOISE, 4000.0f, 40.0f, "Ghost"},
      {SynthMode::NOISE, 6000.0f, 100.0f, "Open HH"}}},

    // 9: Techno
    {{{SynthMode::NOISE, 10000.0f, 20.0f, "HiHat"},
      {SynthMode::NOISE, 12000.0f, 12.0f, "HiHat Ac"},
      {SynthMode::SINE, 42.0f, 250.0f, "909 Kick", 160.f, 1.2f},
      {SynthMode::SINE, 55.0f, 180.0f, "Kick Lay", 100.f, 1.0f},
      {SynthMode::NOISE, 1800.0f, 55.0f, "Clap"},
      {SynthMode::NOISE, 3000.0f, 35.0f, "Rim"},
      {SynthMode::NOISE, 5000.0f, 80.0f, "Open HH"},
      {SynthMode::SINE, 600.0f, 60.0f, "Tom"}}}
};

// Apply preset for specific role (2 voices)
inline void urApplyRolePreset(URExtendedDrumSynth& synth, int role, int styleIndex) {
    if (styleIndex < 0 || styleIndex > 9) return;
    if (role < 0 || role > 3) return;
    const URExtendedStylePreset& preset = UR_EXTENDED_PRESETS[styleIndex];
    int voiceBase = role * 2;
    synth.setVoiceParams(voiceBase, preset.voices[voiceBase].mode,
                         preset.voices[voiceBase].freq, preset.voices[voiceBase].decay,
                         preset.voices[voiceBase].sweep, preset.voices[voiceBase].bend);
    synth.setVoiceParams(voiceBase + 1, preset.voices[voiceBase + 1].mode,
                         preset.voices[voiceBase + 1].freq, preset.voices[voiceBase + 1].decay,
                         preset.voices[voiceBase + 1].sweep, preset.voices[voiceBase + 1].bend);
}

// ============================================================================
// ThreeBandIsolator - Linkwitz-Riley 4th order crossover (UniRhythm namespace)
// ============================================================================

class URThreeBandIsolator {
private:
    float sampleRate = 44100.0f;

    struct Biquad {
        float a0 = 0, a1 = 0, a2 = 0, b1 = 0, b2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        void reset() { x1 = x2 = y1 = y2 = 0; }

        float process(float in) {
            float out = a0 * in + a1 * x1 + a2 * x2 - b1 * y1 - b2 * y2;
            x2 = x1; x1 = in;
            y2 = y1; y1 = out;
            return out;
        }
    };

    Biquad lpLow1[2], lpLow2[2];
    Biquad hpLow1[2], hpLow2[2];
    Biquad lpHigh1[2], lpHigh2[2];
    Biquad hpHigh1[2], hpHigh2[2];

    void calcButterworth2LP(Biquad& bq, float fc) {
        float w0 = 2.0f * M_PI * fc / sampleRate;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / std::sqrt(2.0f);
        float norm = 1.0f / (1.0f + alpha);
        bq.a0 = (1.0f - cosw0) * 0.5f * norm;
        bq.a1 = (1.0f - cosw0) * norm;
        bq.a2 = bq.a0;
        bq.b1 = -2.0f * cosw0 * norm;
        bq.b2 = (1.0f - alpha) * norm;
    }

    void calcButterworth2HP(Biquad& bq, float fc) {
        float w0 = 2.0f * M_PI * fc / sampleRate;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / std::sqrt(2.0f);
        float norm = 1.0f / (1.0f + alpha);
        bq.a0 = (1.0f + cosw0) * 0.5f * norm;
        bq.a1 = -(1.0f + cosw0) * norm;
        bq.a2 = bq.a0;
        bq.b1 = -2.0f * cosw0 * norm;
        bq.b2 = (1.0f - alpha) * norm;
    }

public:
    void setSampleRate(float sr) {
        sampleRate = sr;
        for (int ch = 0; ch < 2; ch++) {
            calcButterworth2LP(lpLow1[ch], 250.0f);
            calcButterworth2LP(lpLow2[ch], 250.0f);
            calcButterworth2HP(hpLow1[ch], 250.0f);
            calcButterworth2HP(hpLow2[ch], 250.0f);
            calcButterworth2LP(lpHigh1[ch], 4000.0f);
            calcButterworth2LP(lpHigh2[ch], 4000.0f);
            calcButterworth2HP(hpHigh1[ch], 4000.0f);
            calcButterworth2HP(hpHigh2[ch], 4000.0f);
        }
        reset();
    }

    void reset() {
        for (int ch = 0; ch < 2; ch++) {
            lpLow1[ch].reset(); lpLow2[ch].reset();
            hpLow1[ch].reset(); hpLow2[ch].reset();
            lpHigh1[ch].reset(); lpHigh2[ch].reset();
            hpHigh1[ch].reset(); hpHigh2[ch].reset();
        }
    }

    void process(float& left, float& right, float lowParam, float midParam, float highParam) {
        auto paramToGain = [](float p) {
            if (p < 0) {
                float t = 1.0f + p;
                return t * t;
            } else {
                return 1.0f + p * 3.0f;
            }
        };

        float gainLow = paramToGain(lowParam);
        float gainMid = paramToGain(midParam);
        float gainHigh = paramToGain(highParam);

        float ins[2] = {left, right};
        float outs[2];

        for (int ch = 0; ch < 2; ch++) {
            float in = ins[ch];
            float low = lpLow2[ch].process(lpLow1[ch].process(in));
            float high = hpHigh2[ch].process(hpHigh1[ch].process(in));
            float midTemp = hpLow2[ch].process(hpLow1[ch].process(in));
            float mid = lpHigh2[ch].process(lpHigh1[ch].process(midTemp));
            outs[ch] = low * gainLow + mid * gainMid + high * gainHigh;
        }

        left = outs[0];
        right = outs[1];
    }
};

// ============================================================================
// TubeDrive - Asymmetric tube saturation with DC blocker (UniRhythm namespace)
// ============================================================================

class URTubeDrive {
private:
    float sampleRate = 44100.0f;
    float dcBlockerL = 0, dcBlockerR = 0;
    float dcCoeff = 0.999f;

public:
    void setSampleRate(float sr) {
        sampleRate = sr;
        float fc = 10.0f;
        dcCoeff = 1.0f - (2.0f * M_PI * fc / sr);
        if (dcCoeff < 0.9f) dcCoeff = 0.9f;
        if (dcCoeff > 0.9999f) dcCoeff = 0.9999f;
    }

    void reset() {
        dcBlockerL = dcBlockerR = 0;
    }

    void process(float& left, float& right, float driveAmount) {
        if (driveAmount < 0.01f) return;

        auto tubeShape = [](float x, float drive) {
            float scaled = x * (1.0f + drive * 2.0f);
            if (scaled >= 0) {
                return std::tanh(scaled * 0.8f);
            } else {
                return std::tanh(scaled * 1.0f);
            }
        };

        float makeupGain = 1.0f / (1.0f + driveAmount * 0.5f);
        left = tubeShape(left, driveAmount) * makeupGain;
        right = tubeShape(right, driveAmount) * makeupGain;

        float prevL = dcBlockerL;
        float prevR = dcBlockerR;
        dcBlockerL = left - prevL + dcCoeff * dcBlockerL;
        dcBlockerR = right - prevR + dcCoeff * dcBlockerR;
        left = dcBlockerL;
        right = dcBlockerR;
    }
};

// ============================================================================
// Pattern storage for 8 voices (UniRhythm namespace)
// ============================================================================

struct URMultiVoicePatterns {
    WorldRhythm::Pattern patterns[8];

    URMultiVoicePatterns() {
        for (int i = 0; i < 8; i++) {
            patterns[i] = WorldRhythm::Pattern(16);
        }
    }

    void clear() {
        for (int i = 0; i < 8; i++) {
            patterns[i].clear();
        }
    }
};

// Helper: find max element in int array (replaces std::max_element)
static inline int urMaxElement(const int* arr, int count) {
    int m = arr[0];
    for (int i = 1; i < count; i++) {
        if (arr[i] > m) m = arr[i];
    }
    return m;
}

} // namespace unirhythm

// ============================================================================
// Uni Rhythm Module (MetaModule port)
// ============================================================================

struct UniRhythm : Module {

    enum ParamId {
        // Per-role parameters (4 roles x 5 params: Style, Density, Length, Freq, Decay)
        TIMELINE_STYLE_PARAM,
        TIMELINE_DENSITY_PARAM,
        TIMELINE_LENGTH_PARAM,
        TIMELINE_FREQ_PARAM,
        TIMELINE_DECAY_PARAM,
        FOUNDATION_STYLE_PARAM,
        FOUNDATION_DENSITY_PARAM,
        FOUNDATION_LENGTH_PARAM,
        FOUNDATION_FREQ_PARAM,
        FOUNDATION_DECAY_PARAM,
        GROOVE_STYLE_PARAM,
        GROOVE_DENSITY_PARAM,
        GROOVE_LENGTH_PARAM,
        GROOVE_FREQ_PARAM,
        GROOVE_DECAY_PARAM,
        LEAD_STYLE_PARAM,
        LEAD_DENSITY_PARAM,
        LEAD_LENGTH_PARAM,
        LEAD_FREQ_PARAM,
        LEAD_DECAY_PARAM,
        // Global parameters
        VARIATION_PARAM,
        HUMANIZE_PARAM,
        SWING_PARAM,
        REST_PARAM,
        FILL_PARAM,
        ARTICULATION_PARAM,
        GHOST_PARAM,
        ACCENT_PROB_PARAM,
        SPREAD_PARAM,
        REGENERATE_PARAM,
        RESET_BUTTON_PARAM,
        // Mix parameters (per-role)
        TIMELINE_MIX_PARAM,
        FOUNDATION_MIX_PARAM,
        GROOVE_MIX_PARAM,
        LEAD_MIX_PARAM,
        // Master Isolator + Drive
        ISO_LOW_PARAM,
        ISO_MID_PARAM,
        ISO_HIGH_PARAM,
        DRIVE_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        REGENERATE_INPUT,
        REST_CV_INPUT,
        FILL_INPUT,
        TIMELINE_STYLE_CV_INPUT,
        TIMELINE_DENSITY_CV_INPUT,
        TIMELINE_FREQ_CV_INPUT,
        TIMELINE_DECAY_CV_INPUT,
        FOUNDATION_STYLE_CV_INPUT,
        FOUNDATION_DENSITY_CV_INPUT,
        FOUNDATION_FREQ_CV_INPUT,
        FOUNDATION_DECAY_CV_INPUT,
        GROOVE_STYLE_CV_INPUT,
        GROOVE_DENSITY_CV_INPUT,
        GROOVE_FREQ_CV_INPUT,
        GROOVE_DECAY_CV_INPUT,
        LEAD_STYLE_CV_INPUT,
        LEAD_DENSITY_CV_INPUT,
        LEAD_FREQ_CV_INPUT,
        LEAD_DECAY_CV_INPUT,
        TIMELINE_AUDIO_INPUT_1,
        TIMELINE_AUDIO_INPUT_2,
        FOUNDATION_AUDIO_INPUT_1,
        FOUNDATION_AUDIO_INPUT_2,
        GROOVE_AUDIO_INPUT_1,
        GROOVE_AUDIO_INPUT_2,
        LEAD_AUDIO_INPUT_1,
        LEAD_AUDIO_INPUT_2,
        INPUTS_LEN
    };

    enum OutputId {
        TIMELINE_AUDIO_OUTPUT,
        TIMELINE_GATE_OUTPUT,
        TIMELINE_PITCH_OUTPUT,
        TIMELINE_VELENV_OUTPUT,
        FOUNDATION_AUDIO_OUTPUT,
        FOUNDATION_GATE_OUTPUT,
        FOUNDATION_PITCH_OUTPUT,
        FOUNDATION_VELENV_OUTPUT,
        GROOVE_AUDIO_OUTPUT,
        GROOVE_GATE_OUTPUT,
        GROOVE_PITCH_OUTPUT,
        GROOVE_VELENV_OUTPUT,
        LEAD_AUDIO_OUTPUT,
        LEAD_GATE_OUTPUT,
        LEAD_PITCH_OUTPUT,
        LEAD_VELENV_OUTPUT,
        MIX_L_OUTPUT,
        MIX_R_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightId {
        TIMELINE_LIGHT,
        FOUNDATION_LIGHT,
        GROOVE_LIGHT,
        LEAD_LIGHT,
        CLOCK_LIGHT,
        LIGHTS_LEN
    };

    // Engines
    WorldRhythm::PatternGenerator patternGen;
    WorldRhythm::HumanizeEngine humanize;
    WorldRhythm::RestEngine restEngine;
    WorldRhythm::FillGenerator fillGen;
    WorldRhythm::ArticulationEngine articulationEngine;
    WorldRhythm::KotekanEngine kotekanEngine;
    WorldRhythm::LlamadaEngine llamadaEngine;
    WorldRhythm::CrossRhythmEngine crossRhythmEngine;
    WorldRhythm::AsymmetricGroupingEngine asymmetricEngine;
    WorldRhythm::AmenBreakEngine amenBreakEngine;
    unirhythm::URExtendedDrumSynth drumSynth;

    // Master Isolator + Drive
    unirhythm::URThreeBandIsolator isolator;
    unirhythm::URTubeDrive tubeDrive;

    // Pattern storage
    unirhythm::URMultiVoicePatterns patterns;
    unirhythm::URMultiVoicePatterns originalPatterns;
    int roleLengths[4] = {16, 16, 16, 16};
    int currentSteps[4] = {0, 0, 0, 0};
    int currentBar = 0;
    float appliedRest = 0.0f;

    // Cached synth parameters
    float cachedFreqs[8] = {0};
    float cachedDecays[8] = {0};
    float cachedSweeps[8] = {0};
    float cachedBends[8] = {1,1,1,1,1,1,1,1};
    float currentFreqs[8] = {0};

    // Triggers and pulses
    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger resetButtonTrigger;
    dsp::SchmittTrigger regenerateTrigger;
    dsp::SchmittTrigger regenerateButtonTrigger;
    dsp::SchmittTrigger fillTrigger;

    // Fill state
    bool fillActive = false;
    int fillStepsRemaining = 0;
    unirhythm::URMultiVoicePatterns fillPatterns;
    WorldRhythm::FillType currentFillType = WorldRhythm::FillType::NONE;

    bool nextBarHasFill = false;
    int fillStartStep = 0;
    int fillLengthStepsPlanned = 0;

    // Primary Priority Merge
    bool lastTriggerWasPrimary[4] = {true, true, true, true};
    float currentPitches[4] = {0.f, 0.f, 0.f, 0.f};

    dsp::PulseGenerator gatePulses[4];
    dsp::PulseGenerator accentPulses[8];
    dsp::PulseGenerator clockPulse;

    float currentVelocities[8] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    bool currentAccents[8] = {false, false, false, false, false, false, false, false};

    int globalStep = 0;
    int ppqn = 4;
    int ppqnCounter = 0;

    // Flam/Drag delayed trigger support (fixed array)
    static constexpr int MAX_DELAYED_TRIGGERS = 64;
    struct DelayedTrigger {
        float samplesRemaining = 0;
        int voice = -1;
        float velocity = 0;
        bool isAccent = false;
        int role = 0;
        bool isStrongBeat = false;
        bool isSubNote = false;
    };
    DelayedTrigger delayedTriggers[MAX_DELAYED_TRIGGERS];
    int delayedTriggerCount = 0;

    // Change detection
    int lastStyles[4] = {-1, -1, -1, -1};
    float lastDensities[4] = {-1.f, -1.f, -1.f, -1.f};
    int lastLengths[4] = {-1, -1, -1, -1};
    float lastVariation = -1.0f;
    float lastRoleFreqs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float lastRoleDecays[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float lastSwing = 0.5f;

    // External audio VCA envelopes
    struct VCAEnvelope {
        float amplitude = 0.0f;
        float decayRate = 0.0f;

        void trigger(float decayTimeMs, float sr, float velocity = 1.0f) {
            amplitude = 1.0f;
            float velScale = 0.1f + 0.9f * std::pow(velocity, 1.5f);
            float actualDecayMs = decayTimeMs * velScale;
            decayRate = 1.0f / (actualDecayMs * 0.001f * sr);
        }

        float process() {
            if (amplitude > 0.0f) {
                float current = amplitude;
                amplitude -= decayRate;
                if (amplitude < 0.0f) amplitude = 0.0f;
                return current;
            }
            return 0.0f;
        }

        bool isActive() const {
            return amplitude > 0.001f;
        }
    };

    VCAEnvelope externalVCA[8];
    float currentMix[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Velocity Envelope for CV output
    struct VelocityEnvelope {
        enum Phase { IDLE, ATTACK, DECAY };
        Phase phase = IDLE;
        float output = 0.0f;
        float phaseTime = 0.0f;
        float peakVoltage = 0.0f;
        float attackTime = 0.0003f;
        float currentDecayTime = 1.0f;
        float curve = -0.95f;

        float applyCurve(float x, float curvature) {
            x = clamp(x, 0.0f, 1.0f);
            if (curvature == 0.0f) return x;
            float k = curvature;
            float abs_x = std::abs(x);
            float denominator = k - 2.0f * k * abs_x + 1.0f;
            if (std::abs(denominator) < 1e-6f) return x;
            return (x - k * x) / denominator;
        }

        void trigger(float decayParam, float sr, float velocity) {
            peakVoltage = velocity * 8.0f;
            phase = ATTACK;
            phaseTime = 0.0f;
            float sqrtDecay = std::pow(decayParam, 0.33f);
            float mappedDecay = rescale(sqrtDecay, 0.0f, 1.0f, 0.0f, 0.8f);
            currentDecayTime = std::pow(10.0f, (mappedDecay - 0.8f) * 5.0f);
            if (currentDecayTime < 0.01f) currentDecayTime = 0.01f;
        }

        float process(float sampleTime) {
            switch (phase) {
                case IDLE:
                    output = 0.0f;
                    break;
                case ATTACK:
                    phaseTime += sampleTime;
                    if (phaseTime >= attackTime) {
                        phase = DECAY;
                        phaseTime = 0.0f;
                        output = 1.0f;
                    } else {
                        float t = phaseTime / attackTime;
                        output = applyCurve(t, curve);
                    }
                    break;
                case DECAY:
                    phaseTime += sampleTime;
                    if (phaseTime >= currentDecayTime) {
                        output = 0.0f;
                        phase = IDLE;
                        phaseTime = 0.0f;
                    } else {
                        float t = phaseTime / currentDecayTime;
                        output = 1.0f - applyCurve(t, curve);
                    }
                    break;
            }
            output = clamp(output, 0.0f, 1.0f);
            return output * peakVoltage;
        }
    };

    VelocityEnvelope velocityEnv[4];

    // 3-tier Articulation helper functions
    float getGhostAmount() {
        float art = params[ARTICULATION_PARAM].getValue();
        if (art <= 0.33f) return art / 0.33f;
        return 1.0f;
    }

    float getAccentAmount() {
        float art = params[ARTICULATION_PARAM].getValue();
        if (art <= 0.33f) return 0.0f;
        if (art <= 0.66f) return (art - 0.33f) / 0.33f;
        return 1.0f;
    }

    float getArticulationAmount() {
        float art = params[ARTICULATION_PARAM].getValue();
        if (art <= 0.66f) return 0.0f;
        return (art - 0.66f) / 0.34f;
    }

    UniRhythm() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        for (int r = 0; r < 4; r++) {
            int baseParam = r * 5;
            configParam<unirhythm::URStyleParamQuantity>(TIMELINE_STYLE_PARAM + baseParam, 0.0f, 9.0f, 0.0f, "Style");
            getParamQuantity(TIMELINE_STYLE_PARAM + baseParam)->snapEnabled = true;

            configParam(TIMELINE_DENSITY_PARAM + baseParam, 0.0f, 0.9f,
                        r == 1 ? 0.2f : (r == 0 ? 0.4f : 0.5f), "Density", "%", 0.0f, 100.0f);

            configParam(TIMELINE_LENGTH_PARAM + baseParam, 4.0f, 32.0f, 16.0f, "Length");
            getParamQuantity(TIMELINE_LENGTH_PARAM + baseParam)->snapEnabled = true;

            configParam(TIMELINE_FREQ_PARAM + baseParam, -1.0f, 1.0f, 0.0f, "Freq", " oct");
            configParam(TIMELINE_DECAY_PARAM + baseParam, 0.2f, 2.0f, 1.0f, "Decay", "x");
        }

        for (int r = 0; r < 4; r++) {
            configParam(TIMELINE_MIX_PARAM + r, 0.0f, 1.0f, 0.0f, "Mix", "%", 0.0f, 100.0f);
        }

        configParam(VARIATION_PARAM, 0.0f, 1.0f, 0.3f, "Variation", "%", 0.0f, 100.0f);
        configParam(HUMANIZE_PARAM, 0.0f, 1.0f, 0.5f, "Humanize", "%", 0.0f, 100.0f);
        configParam(SWING_PARAM, 0.0f, 1.0f, 0.5f, "Swing", "%", 0.0f, 100.0f);
        configParam(REST_PARAM, 0.0f, 1.0f, 0.0f, "Rest", "%", 0.0f, 100.0f);
        configParam(FILL_PARAM, 0.0f, 1.0f, 0.3f, "Fill", "%", 0.0f, 100.0f);
        configParam(ARTICULATION_PARAM, 0.0f, 1.0f, 0.0f, "Articulation", "%", 0.0f, 100.0f);
        configParam(GHOST_PARAM, 0.0f, 1.0f, 0.0f, "Ghost Notes", "%", 0.0f, 100.0f);
        configParam(ACCENT_PROB_PARAM, 0.0f, 1.0f, 0.0f, "Accent", "%", 0.0f, 100.0f);
        configParam(SPREAD_PARAM, 0.0f, 1.0f, 0.5f, "Spread", "%", 0.0f, 100.0f);
        configParam(REGENERATE_PARAM, 0.0f, 1.0f, 0.0f, "Regenerate");
        configParam(RESET_BUTTON_PARAM, 0.0f, 1.0f, 0.0f, "Reset");

        configParam(ISO_LOW_PARAM, -1.0f, 1.0f, 0.0f, "Isolator Low");
        configParam(ISO_MID_PARAM, -1.0f, 1.0f, 0.0f, "Isolator Mid");
        configParam(ISO_HIGH_PARAM, -1.0f, 1.0f, 0.0f, "Isolator High");
        configParam(DRIVE_PARAM, 0.0f, 1.0f, 0.0f, "Master Drive", "%", 0.0f, 100.0f);

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(REGENERATE_INPUT, "Regenerate");
        configInput(REST_CV_INPUT, "Rest CV");
        configInput(FILL_INPUT, "Fill Trigger");

        for (int r = 0; r < 4; r++) {
            configInput(TIMELINE_STYLE_CV_INPUT + r * 4, "Style CV");
            configInput(TIMELINE_DENSITY_CV_INPUT + r * 4, "Density CV");
            configInput(TIMELINE_FREQ_CV_INPUT + r * 4, "Freq CV");
            configInput(TIMELINE_DECAY_CV_INPUT + r * 4, "Decay CV");
        }

        for (int r = 0; r < 4; r++) {
            configInput(TIMELINE_AUDIO_INPUT_1 + r * 2, "Audio Input 1");
            configInput(TIMELINE_AUDIO_INPUT_2 + r * 2, "Audio Input 2");
        }

        for (int role = 0; role < 4; role++) {
            configOutput(TIMELINE_AUDIO_OUTPUT + role * 4, "Audio");
            configOutput(TIMELINE_GATE_OUTPUT + role * 4, "Gate");
            configOutput(TIMELINE_PITCH_OUTPUT + role * 4, "Pitch CV");
            configOutput(TIMELINE_VELENV_OUTPUT + role * 4, "Velocity Envelope");
        }
        configOutput(MIX_L_OUTPUT, "Mix L");
        configOutput(MIX_R_OUTPUT, "Mix R");

        regenerateAllPatterns();
    }

    void onSampleRateChange() override {
        drumSynth.setSampleRate(APP->engine->getSampleRate());
        isolator.setSampleRate(APP->engine->getSampleRate());
        tubeDrive.setSampleRate(APP->engine->getSampleRate());
    }

    void onReset() override {
        for (int i = 0; i < 4; i++) currentSteps[i] = 0;
        currentBar = 0;
        globalStep = 0;
        ppqnCounter = 0;
        fillActive = false;
        fillStepsRemaining = 0;
        regenerateAllPatterns();
    }

    void resetSteps() {
        for (int i = 0; i < 4; i++) currentSteps[i] = 0;
        currentBar = 0;
        globalStep = 0;
        ppqnCounter = 0;
        fillActive = false;
        fillStepsRemaining = 0;
        nextBarHasFill = false;
        fillStartStep = 0;
        fillLengthStepsPlanned = 0;
    }

    void applySynthModifiers() {
        for (int role = 0; role < 4; role++) {
            int baseParam = role * 5;
            float freqAmount = params[TIMELINE_FREQ_PARAM + baseParam].getValue();
            float decayMult = params[TIMELINE_DECAY_PARAM + baseParam].getValue();

            if (inputs[TIMELINE_FREQ_CV_INPUT + role * 4].isConnected()) {
                freqAmount += inputs[TIMELINE_FREQ_CV_INPUT + role * 4].getVoltage() * 0.2f;
                freqAmount = clamp(freqAmount, -1.0f, 1.0f);
            }
            if (inputs[TIMELINE_DECAY_CV_INPUT + role * 4].isConnected()) {
                decayMult += inputs[TIMELINE_DECAY_CV_INPUT + role * 4].getVoltage() * 0.18f;
                decayMult = clamp(decayMult, 0.2f, 2.0f);
            }

            float freqMult = std::pow(2.0f, freqAmount);
            int voiceBase = role * 2;
            for (int v = 0; v < 2; v++) {
                int voiceIdx = voiceBase + v;
                if (cachedFreqs[voiceIdx] > 0) {
                    float newFreq = cachedFreqs[voiceIdx] * freqMult;
                    float newDecay = cachedDecays[voiceIdx] * decayMult;
                    currentFreqs[voiceIdx] = newFreq;
                    int styleIndex = lastStyles[role];
                    if (styleIndex >= 0 && styleIndex <= 9) {
                        const unirhythm::URExtendedStylePreset& preset = unirhythm::UR_EXTENDED_PRESETS[styleIndex];
                        drumSynth.setVoiceParams(voiceIdx, preset.voices[voiceIdx].mode, newFreq, newDecay,
                                                 cachedSweeps[voiceIdx], cachedBends[voiceIdx]);
                    }
                }
            }
            lastRoleFreqs[role] = freqAmount;
            lastRoleDecays[role] = decayMult;
        }
    }

    void regenerateAllPatternsInterlocked() {
        float variation = params[VARIATION_PARAM].getValue();
        float restAmount = params[REST_PARAM].getValue();
        if (inputs[REST_CV_INPUT].isConnected()) {
            restAmount += inputs[REST_CV_INPUT].getVoltage() * 0.1f;
            restAmount = clamp(restAmount, 0.0f, 1.0f);
        }
        float humanizeAmount = params[HUMANIZE_PARAM].getValue();
        float swingAmount = params[SWING_PARAM].getValue();

        int mainStyleIndex = static_cast<int>(params[TIMELINE_STYLE_PARAM].getValue());
        mainStyleIndex = clamp(mainStyleIndex, 0, WorldRhythm::NUM_STYLES - 1);
        const WorldRhythm::StyleProfile& mainStyle = *WorldRhythm::STYLES[mainStyleIndex];

        WorldRhythm::PatternGenerator::InterlockConfig config =
            WorldRhythm::PatternGenerator::getStyleInterlockConfig(mainStyleIndex);

        int baseLength = static_cast<int>(params[TIMELINE_LENGTH_PARAM].getValue());
        float baseDensity = params[TIMELINE_DENSITY_PARAM].getValue();

        WorldRhythm::PatternGenerator::RolePatterns interlocked =
            patternGen.generateInterlocked(mainStyle, baseLength, baseDensity, variation, config);

        patterns.patterns[0] = interlocked.timeline;
        patterns.patterns[2] = interlocked.foundation;
        patterns.patterns[4] = interlocked.groove;
        patterns.patterns[6] = interlocked.lead;

        for (int r = 0; r < 4; r++) {
            int bParam = r * 5;
            float styleCV = 0.0f;
            if (inputs[TIMELINE_STYLE_CV_INPUT + r * 4].isConnected())
                styleCV = inputs[TIMELINE_STYLE_CV_INPUT + r * 4].getVoltage();
            int styleIndex = static_cast<int>(params[TIMELINE_STYLE_PARAM + bParam].getValue() + styleCV);
            styleIndex = clamp(styleIndex, 0, WorldRhythm::NUM_STYLES - 1);

            float density = params[TIMELINE_DENSITY_PARAM + bParam].getValue();
            int length = static_cast<int>(params[TIMELINE_LENGTH_PARAM + bParam].getValue());
            roleLengths[r] = length;

            const WorldRhythm::StyleProfile& style = *WorldRhythm::STYLES[styleIndex];
            WorldRhythm::Role roleType = static_cast<WorldRhythm::Role>(r);

            if (density < 0.01f) {
                patterns.patterns[r * 2] = WorldRhythm::Pattern(length);
                patterns.patterns[r * 2 + 1] = WorldRhythm::Pattern(length);
                lastStyles[r] = styleIndex; lastDensities[r] = density; lastLengths[r] = length;
                continue;
            }

            if (length != baseLength || std::abs(density - baseDensity) > 0.05f || styleIndex != mainStyleIndex)
                patterns.patterns[r * 2] = patternGen.generate(roleType, style, length, density, variation);

            if (styleIndex == 5 && (r == 2 || r == 3)) {
                WorldRhythm::KotekanType kotekanType = kotekanEngine.getRecommendedType(styleIndex);
                kotekanEngine.setType(kotekanType); kotekanEngine.setIntensity(1.0f);
                WorldRhythm::KotekanPair kotekan = kotekanEngine.generate(length, 0.8f, density);
                patterns.patterns[r * 2] = kotekan.polos; patterns.patterns[r * 2 + 1] = kotekan.sangsih;
            } else if (styleIndex == 8) {
                if (r == 1) { patterns.patterns[r*2] = amenBreakEngine.generateKick(length, density); patterns.patterns[r*2+1] = amenBreakEngine.generateKick(length, density*0.7f); }
                else if (r == 2) { patterns.patterns[r*2] = amenBreakEngine.generateSnare(length, density); patterns.patterns[r*2+1] = amenBreakEngine.generateSnare(length, density*0.6f); }
                else if (r == 3) { patterns.patterns[r*2] = amenBreakEngine.generateRandomChop(length, density, variation); patterns.patterns[r*2+1] = amenBreakEngine.generateHihat(length, density*0.8f); }
                else { patterns.patterns[r*2+1] = patternGen.generateWithInterlock(roleType, style, length, density*0.5f, variation+0.2f, patterns.patterns[r*2]); }
            } else {
                patterns.patterns[r*2+1] = patternGen.generateWithInterlock(roleType, style, length, density*0.5f, variation+0.2f, patterns.patterns[r*2]);
            }

            if ((styleIndex == 0 || styleIndex == 1 || styleIndex == 2) && r == 2) {
                WorldRhythm::CrossRhythmType crType = crossRhythmEngine.getStyleCrossRhythm(styleIndex);
                float crIntensity = crossRhythmEngine.getStyleCrossRhythmIntensity(styleIndex);
                crossRhythmEngine.applyCrossRhythmOverlay(patterns.patterns[r*2], crType, crIntensity, 0.6f);
                crossRhythmEngine.applyCrossRhythmOverlay(patterns.patterns[r*2+1], crType, crIntensity*0.7f, 0.4f);
            }

            if (styleIndex == 3 || styleIndex == 4) {
                WorldRhythm::GroupingType groupType = WorldRhythm::AsymmetricGroupingEngine::getStyleDefaultGrouping(styleIndex);
                asymmetricEngine.setGroupingType(groupType);
                float intens = (styleIndex == 3) ? 0.8f : 0.6f;
                float secIntens = (styleIndex == 3) ? 0.6f : 0.45f;
                asymmetricEngine.applyToPattern(patterns.patterns[r*2], intens);
                asymmetricEngine.applyToPattern(patterns.patterns[r*2+1], secIntens);
            }

            if (humanizeAmount > 0.01f) {
                humanize.setStyle(styleIndex); humanize.setSwing(swingAmount); humanize.setGrooveForStyle(styleIndex);
                humanize.humanizePattern(patterns.patterns[r*2], roleType, currentBar, 4);
                humanize.humanizePattern(patterns.patterns[r*2+1], roleType, currentBar, 4);
            }

            patternGen.generateAccents(patterns.patterns[r*2], roleType, style);
            patternGen.generateAccents(patterns.patterns[r*2+1], roleType, style);

            float accentAmt = getAccentAmount();
            if (accentAmt > 0.01f) {
                for (int i = 0; i < patterns.patterns[r*2].length; i++) {
                    if (patterns.patterns[r*2].hasOnsetAt(i) && !patterns.patterns[r*2].accents[i]) {
                        float prob = (i % 4 == 0) ? accentAmt : accentAmt * 0.5f;
                        if ((float)rand() / RAND_MAX < prob) patterns.patterns[r*2].accents[i] = true;
                    }
                    if (patterns.patterns[r*2+1].hasOnsetAt(i) && !patterns.patterns[r*2+1].accents[i]) {
                        float prob = (i % 4 == 0) ? accentAmt : accentAmt * 0.5f;
                        if ((float)rand() / RAND_MAX < prob) patterns.patterns[r*2+1].accents[i] = true;
                    }
                }
            }

            float ghostAmt = getGhostAmount();
            if (ghostAmt > 0.01f) {
                float rm = (r == 2 || r == 3) ? 1.0f : 0.5f;
                patternGen.addGhostNotes(patterns.patterns[r*2], style, ghostAmt * rm);
                patternGen.addGhostNotes(patterns.patterns[r*2+1], style, ghostAmt * rm * 0.8f);
            }

            originalPatterns.patterns[r*2] = patterns.patterns[r*2];
            originalPatterns.patterns[r*2+1] = patterns.patterns[r*2+1];

            if (restAmount > 0.01f) {
                restEngine.setStyle(styleIndex);
                restEngine.applyRest(patterns.patterns[r*2], roleType, restAmount);
                restEngine.applyRest(patterns.patterns[r*2+1], roleType, restAmount);
            }

            const unirhythm::URExtendedStylePreset& preset = unirhythm::UR_EXTENDED_PRESETS[styleIndex];
            int vb = r * 2;
            cachedFreqs[vb] = preset.voices[vb].freq; cachedFreqs[vb+1] = preset.voices[vb+1].freq;
            cachedDecays[vb] = preset.voices[vb].decay; cachedDecays[vb+1] = preset.voices[vb+1].decay;
            cachedSweeps[vb] = preset.voices[vb].sweep; cachedSweeps[vb+1] = preset.voices[vb+1].sweep;
            cachedBends[vb] = preset.voices[vb].bend; cachedBends[vb+1] = preset.voices[vb+1].bend;
            unirhythm::urApplyRolePreset(drumSynth, r, styleIndex);

            lastStyles[r] = styleIndex; lastDensities[r] = density; lastLengths[r] = length;
        }

        applySynthModifiers();
        lastVariation = variation; lastSwing = swingAmount;
    }

    void regenerateRolePattern(int role) {
        int baseParam = role * 5;
        float styleCV = 0.0f;
        if (inputs[TIMELINE_STYLE_CV_INPUT + role * 4].isConnected())
            styleCV = inputs[TIMELINE_STYLE_CV_INPUT + role * 4].getVoltage();
        int styleIndex = static_cast<int>(params[TIMELINE_STYLE_PARAM + baseParam].getValue() + styleCV);
        styleIndex = clamp(styleIndex, 0, WorldRhythm::NUM_STYLES - 1);

        float densityCV = 0.0f;
        if (inputs[TIMELINE_DENSITY_CV_INPUT + role * 4].isConnected())
            densityCV = inputs[TIMELINE_DENSITY_CV_INPUT + role * 4].getVoltage() * 0.1f;
        float density = clamp(params[TIMELINE_DENSITY_PARAM + baseParam].getValue() + densityCV, 0.0f, 0.9f);
        int length = static_cast<int>(params[TIMELINE_LENGTH_PARAM + baseParam].getValue());

        if (density < 0.01f) {
            patterns.patterns[role*2] = WorldRhythm::Pattern(length);
            patterns.patterns[role*2+1] = WorldRhythm::Pattern(length);
            roleLengths[role] = length; lastStyles[role] = styleIndex; lastDensities[role] = density; lastLengths[role] = length;
            const unirhythm::URExtendedStylePreset& preset = unirhythm::UR_EXTENDED_PRESETS[styleIndex];
            int vb = role * 2;
            cachedFreqs[vb] = preset.voices[vb].freq; cachedFreqs[vb+1] = preset.voices[vb+1].freq;
            cachedDecays[vb] = preset.voices[vb].decay; cachedDecays[vb+1] = preset.voices[vb+1].decay;
            cachedSweeps[vb] = preset.voices[vb].sweep; cachedSweeps[vb+1] = preset.voices[vb+1].sweep;
            cachedBends[vb] = preset.voices[vb].bend; cachedBends[vb+1] = preset.voices[vb+1].bend;
            unirhythm::urApplyRolePreset(drumSynth, role, styleIndex);
            return;
        }

        float variation = params[VARIATION_PARAM].getValue();
        float restAmount = params[REST_PARAM].getValue();
        if (inputs[REST_CV_INPUT].isConnected()) {
            restAmount += inputs[REST_CV_INPUT].getVoltage() * 0.1f;
            restAmount = clamp(restAmount, 0.0f, 1.0f);
        }
        float humanizeAmount = params[HUMANIZE_PARAM].getValue();
        float swingAmount = params[SWING_PARAM].getValue();
        roleLengths[role] = length;

        const WorldRhythm::StyleProfile& style = *WorldRhythm::STYLES[styleIndex];
        WorldRhythm::Role roleType = static_cast<WorldRhythm::Role>(role);

        if (role == WorldRhythm::TIMELINE) {
            patterns.patterns[role*2] = patternGen.generate(roleType, style, length, density, variation);
        } else if (role == WorldRhythm::FOUNDATION) {
            WorldRhythm::PatternGenerator::InterlockConfig cfg = WorldRhythm::PatternGenerator::getStyleInterlockConfig(styleIndex);
            if (cfg.avoidFoundationOnTimeline) patterns.patterns[role*2] = patternGen.generateFoundationWithInterlock(style, length, density, variation, patterns.patterns[0], cfg.avoidanceStrength);
            else patterns.patterns[role*2] = patternGen.generateFoundation(style, length, density, variation);
        } else if (role == WorldRhythm::GROOVE) {
            WorldRhythm::PatternGenerator::InterlockConfig cfg = WorldRhythm::PatternGenerator::getStyleInterlockConfig(styleIndex);
            if (cfg.grooveComplementsFoundation) patterns.patterns[role*2] = patternGen.generateGrooveWithComplement(style, length, density, variation, patterns.patterns[2], patterns.patterns[0], cfg);
            else patterns.patterns[role*2] = patternGen.generate(roleType, style, length, density, variation);
        } else {
            WorldRhythm::PatternGenerator::InterlockConfig cfg = WorldRhythm::PatternGenerator::getStyleInterlockConfig(styleIndex);
            if (cfg.leadAvoidsGroove) patterns.patterns[role*2] = patternGen.generateWithInterlock(roleType, style, length, density*0.6f, variation, patterns.patterns[4]);
            else patterns.patterns[role*2] = patternGen.generate(roleType, style, length, density*0.6f, variation);
        }

        if (styleIndex == 5 && (role == 2 || role == 3)) {
            WorldRhythm::KotekanType kt = kotekanEngine.getRecommendedType(styleIndex);
            kotekanEngine.setType(kt); kotekanEngine.setIntensity(density);
            WorldRhythm::KotekanPair kp = kotekanEngine.splitIntoKotekan(patterns.patterns[role*2], 0.5f);
            patterns.patterns[role*2] = kp.polos; patterns.patterns[role*2+1] = kp.sangsih;
        } else if (styleIndex == 8) {
            if (role == 1) { patterns.patterns[role*2] = amenBreakEngine.generateKick(length, density); patterns.patterns[role*2+1] = amenBreakEngine.generateKick(length, density*0.7f); }
            else if (role == 2) { patterns.patterns[role*2] = amenBreakEngine.generateSnare(length, density); patterns.patterns[role*2+1] = amenBreakEngine.generateSnare(length, density*0.6f); }
            else if (role == 3) { patterns.patterns[role*2] = amenBreakEngine.generateRandomChop(length, density, variation); patterns.patterns[role*2+1] = amenBreakEngine.generateHihat(length, density*0.8f); }
            else { patterns.patterns[role*2+1] = patternGen.generateWithInterlock(roleType, style, length, density*0.5f, variation+0.2f, patterns.patterns[role*2]); }
        } else {
            patterns.patterns[role*2+1] = patternGen.generateWithInterlock(roleType, style, length, density*0.5f, variation+0.2f, patterns.patterns[role*2]);
        }

        if ((styleIndex == 0 || styleIndex == 1 || styleIndex == 2) && role == 2) {
            WorldRhythm::CrossRhythmType crType = crossRhythmEngine.getStyleCrossRhythm(styleIndex);
            float crI = crossRhythmEngine.getStyleCrossRhythmIntensity(styleIndex);
            crossRhythmEngine.applyCrossRhythmOverlay(patterns.patterns[role*2], crType, crI, 0.6f);
            crossRhythmEngine.applyCrossRhythmOverlay(patterns.patterns[role*2+1], crType, crI*0.7f, 0.4f);
        }

        if (styleIndex == 3 || styleIndex == 4) {
            WorldRhythm::GroupingType gt = WorldRhythm::AsymmetricGroupingEngine::getStyleDefaultGrouping(styleIndex);
            asymmetricEngine.setGroupingType(gt);
            float intens = (styleIndex == 3) ? 0.8f : 0.6f;
            float secI = (styleIndex == 3) ? 0.6f : 0.45f;
            asymmetricEngine.applyToPattern(patterns.patterns[role*2], intens);
            asymmetricEngine.applyToPattern(patterns.patterns[role*2+1], secI);
        }

        if (humanizeAmount > 0.01f) {
            humanize.setStyle(styleIndex); humanize.setSwing(swingAmount); humanize.setGrooveForStyle(styleIndex);
            humanize.humanizePattern(patterns.patterns[role*2], roleType, currentBar, 4);
            humanize.humanizePattern(patterns.patterns[role*2+1], roleType, currentBar, 4);
        }

        patternGen.generateAccents(patterns.patterns[role*2], roleType, style);
        patternGen.generateAccents(patterns.patterns[role*2+1], roleType, style);

        float accentAmt = getAccentAmount();
        if (accentAmt > 0.01f) {
            for (int i = 0; i < patterns.patterns[role*2].length; i++) {
                if (patterns.patterns[role*2].hasOnsetAt(i) && !patterns.patterns[role*2].accents[i]) {
                    float prob = (i%4==0) ? accentAmt : accentAmt*0.5f;
                    if ((float)rand()/RAND_MAX < prob) patterns.patterns[role*2].accents[i] = true;
                }
                if (patterns.patterns[role*2+1].hasOnsetAt(i) && !patterns.patterns[role*2+1].accents[i]) {
                    float prob = (i%4==0) ? accentAmt : accentAmt*0.5f;
                    if ((float)rand()/RAND_MAX < prob) patterns.patterns[role*2+1].accents[i] = true;
                }
            }
        }

        float ghostAmt = getGhostAmount();
        if (ghostAmt > 0.01f) {
            float rm = (role == WorldRhythm::GROOVE || role == WorldRhythm::LEAD) ? 1.0f : 0.5f;
            patternGen.addGhostNotes(patterns.patterns[role*2], style, ghostAmt * rm);
            patternGen.addGhostNotes(patterns.patterns[role*2+1], style, ghostAmt * rm * 0.8f);
        }

        originalPatterns.patterns[role*2] = patterns.patterns[role*2];
        originalPatterns.patterns[role*2+1] = patterns.patterns[role*2+1];

        if (restAmount > 0.01f) {
            restEngine.setStyle(styleIndex);
            restEngine.applyRest(patterns.patterns[role*2], roleType, restAmount);
            restEngine.applyRest(patterns.patterns[role*2+1], roleType, restAmount);
        }

        const unirhythm::URExtendedStylePreset& preset = unirhythm::UR_EXTENDED_PRESETS[styleIndex];
        int vb = role * 2;
        cachedFreqs[vb] = preset.voices[vb].freq; cachedFreqs[vb+1] = preset.voices[vb+1].freq;
        cachedDecays[vb] = preset.voices[vb].decay; cachedDecays[vb+1] = preset.voices[vb+1].decay;
        cachedSweeps[vb] = preset.voices[vb].sweep; cachedSweeps[vb+1] = preset.voices[vb+1].sweep;
        cachedBends[vb] = preset.voices[vb].bend; cachedBends[vb+1] = preset.voices[vb+1].bend;
        unirhythm::urApplyRolePreset(drumSynth, role, styleIndex);
        applySynthModifiers();
        lastStyles[role] = styleIndex; lastDensities[role] = density; lastLengths[role] = length;
    }

    void regenerateAllPatterns() { regenerateAllPatternsInterlocked(); }

    void reapplyRest(float restAmount) {
        for (int role = 0; role < 4; role++) {
            int bParam = role * 5;
            int styleIndex = static_cast<int>(params[TIMELINE_STYLE_PARAM + bParam].getValue());
            styleIndex = clamp(styleIndex, 0, WorldRhythm::NUM_STYLES - 1);
            WorldRhythm::Role roleType = static_cast<WorldRhythm::Role>(role);
            patterns.patterns[role*2] = originalPatterns.patterns[role*2];
            patterns.patterns[role*2+1] = originalPatterns.patterns[role*2+1];
            if (restAmount > 0.01f) {
                restEngine.setStyle(styleIndex);
                restEngine.applyRest(patterns.patterns[role*2], roleType, restAmount);
                restEngine.applyRest(patterns.patterns[role*2+1], roleType, restAmount);
            }
        }
        appliedRest = restAmount;
    }

    void triggerWithArticulation(int voice, float velocity, bool accent, float sr,
                                  int role = -1, bool isStrongBeat = false) {
        float articulationAmount = getArticulationAmount();
        if (role < 0) role = voice / 2;

        bool isPrimary = (voice % 2 == 0);
        lastTriggerWasPrimary[role] = isPrimary;

        const float C4_FREQ = 261.63f;
        if (currentFreqs[voice] > 0) currentPitches[role] = std::log2(currentFreqs[voice] / C4_FREQ);
        else currentPitches[role] = 0.0f;

        int baseParam = role * 5;
        int currentStyle = static_cast<int>(params[TIMELINE_STYLE_PARAM + baseParam].getValue());
        float decayMult = params[TIMELINE_DECAY_PARAM + baseParam].getValue();
        if (inputs[TIMELINE_DECAY_CV_INPUT + role * 4].isConnected()) {
            decayMult += inputs[TIMELINE_DECAY_CV_INPUT + role * 4].getVoltage() * 0.18f;
            decayMult = clamp(decayMult, 0.2f, 2.0f);
        }
        float decayParam = (decayMult - 0.2f) / 1.8f;

        WorldRhythm::ArticulationType art = WorldRhythm::selectArticulation(currentStyle, role, articulationAmount, accent, isStrongBeat);
        float finalVel = velocity;
        bool triggerEnvHere = true;

        switch (art) {
            case WorldRhythm::ArticulationType::GHOST: finalVel = velocity * 0.2f; drumSynth.triggerVoice(voice, finalVel); gatePulses[role].trigger(0.001f); break;
            case WorldRhythm::ArticulationType::ACCENT: finalVel = (velocity * 1.3f < 1.0f) ? velocity * 1.3f : 1.0f; drumSynth.triggerVoice(voice, finalVel); gatePulses[role].trigger(0.001f); accentPulses[voice].trigger(0.001f); break;
            case WorldRhythm::ArticulationType::RIM: finalVel = velocity * 1.1f; drumSynth.triggerVoice(voice, finalVel); gatePulses[role].trigger(0.001f); break;
            case WorldRhythm::ArticulationType::FLAM: { WorldRhythm::ExpandedHit hit = articulationEngine.generateFlam(velocity); scheduleExpandedHit(voice, hit, accent, sr, role); triggerEnvHere = false; break; }
            case WorldRhythm::ArticulationType::DRAG: { WorldRhythm::ExpandedHit hit = articulationEngine.generateDrag(velocity); scheduleExpandedHit(voice, hit, accent, sr, role); triggerEnvHere = false; break; }
            case WorldRhythm::ArticulationType::BUZZ: { WorldRhythm::ExpandedHit hit = articulationEngine.generateBuzz(velocity, 0.032f, 4); scheduleExpandedHit(voice, hit, accent, sr, role); triggerEnvHere = false; break; }
            case WorldRhythm::ArticulationType::RUFF: { WorldRhythm::ExpandedHit hit = articulationEngine.generateRuff(velocity); scheduleExpandedHit(voice, hit, accent, sr, role); triggerEnvHere = false; break; }
            default: drumSynth.triggerVoice(voice, velocity); gatePulses[role].trigger(0.001f); break;
        }

        currentVelocities[voice] = finalVel;
        currentAccents[voice] = accent;
        if (accent && art != WorldRhythm::ArticulationType::GHOST) accentPulses[voice].trigger(0.001f);
        if (triggerEnvHere) velocityEnv[role].trigger(decayParam, sr, finalVel);
    }

    void scheduleExpandedHit(int voice, const WorldRhythm::ExpandedHit& hit, bool accent, float sr, int role) {
        int baseParam = role * 5;
        float decayMult = params[TIMELINE_DECAY_PARAM + baseParam].getValue();
        if (inputs[TIMELINE_DECAY_CV_INPUT + role * 4].isConnected()) {
            decayMult += inputs[TIMELINE_DECAY_CV_INPUT + role * 4].getVoltage() * 0.18f;
            decayMult = clamp(decayMult, 0.2f, 2.0f);
        }
        float vcaDecayMs = 200.0f * decayMult;
        float decayParam = (decayMult - 0.2f) / 1.8f;

        int noteCount = hit.notes.size();
        for (int i = 0; i < noteCount; i++) {
            const WorldRhythm::ExpandedNote& note = hit.notes[i];
            float timingSeconds = note.timing;

            if (timingSeconds <= 0.0f && i == 0) {
                drumSynth.triggerVoice(voice, note.velocity);
                gatePulses[role].trigger(0.001f);
                currentVelocities[voice] = note.velocity;
                currentAccents[voice] = note.isAccent && accent;
                externalVCA[voice].trigger(vcaDecayMs, sr, note.velocity);
                velocityEnv[role].trigger(decayParam, sr, note.velocity);
                if (note.isAccent && accent) accentPulses[voice].trigger(0.001f);
            } else {
                float delayFromFirst = timingSeconds - hit.notes[0].timing;
                DelayedTrigger dt;
                dt.samplesRemaining = static_cast<int>(sr * delayFromFirst);
                dt.voice = voice; dt.velocity = note.velocity;
                dt.isAccent = note.isAccent && accent;
                dt.role = role; dt.isStrongBeat = false; dt.isSubNote = true;
                if (dt.samplesRemaining > 0) {
                    if (delayedTriggerCount < MAX_DELAYED_TRIGGERS) delayedTriggers[delayedTriggerCount++] = dt;
                } else if (i > 0) {
                    drumSynth.triggerVoice(voice, note.velocity);
                    gatePulses[role].trigger(0.001f);
                    currentVelocities[voice] = note.velocity;
                    currentAccents[voice] = note.isAccent && accent;
                    externalVCA[voice].trigger(vcaDecayMs, sr, note.velocity);
                }
            }
        }
    }

    void generateFillPatterns(float intensity) {
        int mainStyleIndex = static_cast<int>(params[TIMELINE_STYLE_PARAM].getValue());
        mainStyleIndex = clamp(mainStyleIndex, 0, 9);

        int fillLengthSteps = fillLengthStepsPlanned;
        if (fillLengthSteps <= 0) {
            int maxLen = unirhythm::urMaxElement(roleLengths, 4);
            int fillLengthBeats = fillGen.getFillLengthBeats(intensity);
            fillLengthSteps = fillLengthBeats * 4;
            if (fillLengthSteps > maxLen) fillLengthSteps = maxLen;
            if (fillLengthSteps < 4) fillLengthSteps = 4;
        }
        fillStepsRemaining = fillLengthSteps;

        for (int r = 0; r < 4; r++) {
            int bParam = r * 5;
            int styleIndex = static_cast<int>(params[TIMELINE_STYLE_PARAM + bParam].getValue());
            styleIndex = clamp(styleIndex, 0, 9);
            WorldRhythm::Role roleType = static_cast<WorldRhythm::Role>(r);
            WorldRhythm::FillType fillType = fillGen.selectFillType(styleIndex, roleType);

            if (!fillGen.shouldRoleFill(roleType, fillType)) {
                int normalLen = patterns.patterns[r*2].length;
                fillPatterns.patterns[r*2] = WorldRhythm::Pattern(fillLengthSteps);
                fillPatterns.patterns[r*2+1] = WorldRhythm::Pattern(fillLengthSteps);
                for (int i = 0; i < fillLengthSteps; i++) {
                    int srcIdx = i % normalLen;
                    if (patterns.patterns[r*2].hasOnsetAt(srcIdx)) { fillPatterns.patterns[r*2].setOnset(i, patterns.patterns[r*2].getVelocity(srcIdx)); fillPatterns.patterns[r*2].accents[i] = patterns.patterns[r*2].accents[srcIdx]; }
                    if (patterns.patterns[r*2+1].hasOnsetAt(srcIdx)) fillPatterns.patterns[r*2+1].setOnset(i, patterns.patterns[r*2+1].getVelocity(srcIdx));
                }
                continue;
            }

            float roleIntensity = fillGen.getRoleFillIntensity(roleType, intensity);

            if (styleIndex == 1 && (r == 2 || r == 3)) {
                WorldRhythm::LlamadaType lt;
                if (intensity > 0.8f) lt = WorldRhythm::LlamadaType::DIABLO;
                else if (intensity > 0.6f) lt = WorldRhythm::LlamadaType::MAMBO_CALL;
                else if (intensity > 0.4f) lt = WorldRhythm::LlamadaType::MONTUNO_ENTRY;
                else lt = WorldRhythm::LlamadaType::STANDARD;
                llamadaEngine.setType(lt);
                WorldRhythm::Pattern lp = llamadaEngine.generateCall(fillLengthSteps, roleIntensity);
                fillPatterns.patterns[r*2] = llamadaEngine.addVariation(lp, 0.2f);
                fillPatterns.patterns[r*2+1] = llamadaEngine.generateResponse(fillLengthSteps, roleIntensity * 0.8f);
            } else {
                WorldRhythm::FillPattern fv = fillGen.generateFillPattern(fillType, fillLengthSteps, roleIntensity);
                fillPatterns.patterns[r*2] = WorldRhythm::Pattern(fillLengthSteps);
                for (int i = 0; i < fv.length; i++) {
                    if (fv.velocities[i] > 0.01f) { fillPatterns.patterns[r*2].setOnset(i, fv.velocities[i]); if (fv.velocities[i] > 0.75f) fillPatterns.patterns[r*2].accents[i] = true; }
                }
                fillPatterns.patterns[r*2+1] = WorldRhythm::Pattern(fillLengthSteps);
                for (int i = 0; i < fv.length; i += 2) {
                    if (fv.velocities[i] > 0.3f) fillPatterns.patterns[r*2+1].setOnset(i, fv.velocities[i] * 0.7f);
                }
            }
        }
        currentFillType = fillGen.selectFillType(mainStyleIndex, WorldRhythm::GROOVE);
        fillActive = true;
    }

    void process(const ProcessArgs& args) override {
        static bool initialized = false;
        if (!initialized) {
            drumSynth.setSampleRate(args.sampleRate);
            isolator.setSampleRate(args.sampleRate);
            tubeDrive.setSampleRate(args.sampleRate);
            initialized = true;
        }

        // Process delayed triggers (swap-and-pop)
        for (int i = 0; i < delayedTriggerCount; ) {
            delayedTriggers[i].samplesRemaining -= 1.0f;
            if (delayedTriggers[i].samplesRemaining <= 0) {
                int bParam = delayedTriggers[i].role * 5;
                float dm = params[TIMELINE_DECAY_PARAM + bParam].getValue();
                if (inputs[TIMELINE_DECAY_CV_INPUT + delayedTriggers[i].role * 4].isConnected()) { dm += inputs[TIMELINE_DECAY_CV_INPUT + delayedTriggers[i].role * 4].getVoltage() * 0.18f; dm = clamp(dm, 0.2f, 2.0f); }
                float vcaDMs = 200.0f * dm;

                if (!delayedTriggers[i].isSubNote) {
                    triggerWithArticulation(delayedTriggers[i].voice, delayedTriggers[i].velocity, delayedTriggers[i].isAccent, args.sampleRate, delayedTriggers[i].role, delayedTriggers[i].isStrongBeat);
                    externalVCA[delayedTriggers[i].voice].trigger(vcaDMs, args.sampleRate, delayedTriggers[i].velocity);
                } else {
                    drumSynth.triggerVoice(delayedTriggers[i].voice, delayedTriggers[i].velocity);
                    gatePulses[delayedTriggers[i].role].trigger(0.001f);
                    currentVelocities[delayedTriggers[i].voice] = delayedTriggers[i].velocity;
                    currentAccents[delayedTriggers[i].voice] = delayedTriggers[i].isAccent;
                    externalVCA[delayedTriggers[i].voice].trigger(vcaDMs, args.sampleRate, delayedTriggers[i].velocity);
                    if (delayedTriggers[i].isAccent) accentPulses[delayedTriggers[i].voice].trigger(0.001f);
                }
                delayedTriggers[i] = delayedTriggers[--delayedTriggerCount];
            } else { ++i; }
        }

        float variation = params[VARIATION_PARAM].getValue();
        float restAmount = params[REST_PARAM].getValue();
        if (inputs[REST_CV_INPUT].isConnected()) { restAmount += inputs[REST_CV_INPUT].getVoltage() * 0.1f; restAmount = clamp(restAmount, 0.0f, 1.0f); }

        bool globalRegenNeeded = std::abs(variation - lastVariation) > 0.05f;
        bool synthUpdateNeeded = false;
        for (int r = 0; r < 4; r++) {
            int bParam = r * 5;
            if (std::abs(params[TIMELINE_FREQ_PARAM + bParam].getValue() - lastRoleFreqs[r]) > 0.01f ||
                std::abs(params[TIMELINE_DECAY_PARAM + bParam].getValue() - lastRoleDecays[r]) > 0.01f) synthUpdateNeeded = true;
        }

        if (regenerateTrigger.process(inputs[REGENERATE_INPUT].getVoltage()) || regenerateButtonTrigger.process(params[REGENERATE_PARAM].getValue()))
            globalRegenNeeded = true;

        if (synthUpdateNeeded && !globalRegenNeeded) applySynthModifiers();

        for (int r = 0; r < 4; r++) {
            int bParam = r * 5;
            float sCV = 0.0f;
            if (inputs[TIMELINE_STYLE_CV_INPUT + r*4].isConnected()) sCV = inputs[TIMELINE_STYLE_CV_INPUT + r*4].getVoltage();
            int sIdx = static_cast<int>(params[TIMELINE_STYLE_PARAM + bParam].getValue() + sCV);
            sIdx = clamp(sIdx, 0, 9);
            float dCV = 0.0f;
            if (inputs[TIMELINE_DENSITY_CV_INPUT + r*4].isConnected()) dCV = inputs[TIMELINE_DENSITY_CV_INPUT + r*4].getVoltage() * 0.1f;
            float dens = clamp(params[TIMELINE_DENSITY_PARAM + bParam].getValue() + dCV, 0.0f, 0.9f);
            int len = static_cast<int>(params[TIMELINE_LENGTH_PARAM + bParam].getValue());

            bool dBecameZero = (dens < 0.01f && lastDensities[r] >= 0.01f);
            bool dChanged = std::abs(dens - lastDensities[r]) > 0.04f;
            if (globalRegenNeeded || sIdx != lastStyles[r] || dBecameZero || dChanged || len != lastLengths[r])
                regenerateRolePattern(r);
        }

        if (globalRegenNeeded) { lastVariation = variation; appliedRest = restAmount; }
        if (std::abs(restAmount - appliedRest) > 0.03f) reapplyRest(restAmount);

        if (resetTrigger.process(inputs[RESET_INPUT].getVoltage()) || resetButtonTrigger.process(params[RESET_BUTTON_PARAM].getValue()))
            resetSteps();

        float fillAmount = params[FILL_PARAM].getValue();
        if (fillTrigger.process(inputs[FILL_INPUT].getVoltage())) { if (fillAmount > 0.01f) generateFillPatterns(fillAmount); }

        // Process clock
        if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
            clockPulse.trigger(0.001f);
            int stepsPerClock = 4 / ppqn;
            float swingAmount = params[SWING_PARAM].getValue();
            float humanizeAmount = params[HUMANIZE_PARAM].getValue();
            const WorldRhythm::GrooveTemplate& groove = humanize.getCurrentGroove();
            int maxLen = unirhythm::urMaxElement(roleLengths, 4);

            if (globalStep == 0 && !fillActive) {
                if (fillAmount > 0.01f) {
                    int barInPhrase = currentBar % 4;
                    if (barInPhrase == 3) { nextBarHasFill = true; fillLengthStepsPlanned = 8; }
                    else if (barInPhrase == 1 && fillAmount > 0.3f) { nextBarHasFill = true; fillLengthStepsPlanned = 4; }
                    else nextBarHasFill = false;
                    if (nextBarHasFill) {
                        if (fillLengthStepsPlanned > maxLen) fillLengthStepsPlanned = maxLen;
                        fillStartStep = maxLen - fillLengthStepsPlanned;
                        if (fillStartStep < 0) fillStartStep = 0;
                    }
                } else nextBarHasFill = false;
            }

            if (nextBarHasFill && !fillActive && globalStep == fillStartStep) { generateFillPatterns(fillAmount); nextBarHasFill = false; }

            for (int stepOffset = 0; stepOffset < stepsPerClock; stepOffset++) {
                for (int r = 0; r < 4; r++) {
                    int step = currentSteps[r];
                    int vb = r * 2;
                    int fpLen = static_cast<int>(fillPatterns.patterns[vb].length);
                    int fStep = fillActive ? (fpLen - fillStepsRemaining) : step;
                    if (fillActive && fStep < 0) fStep = 0;
                    if (fillActive && fStep >= fpLen) fStep = fpLen - 1;
                    int useStep = fillActive ? fStep : step;

                    int pos = useStep % 16;
                    float grooveOffsetMs = groove.offsets[pos] * humanizeAmount;
                    float swingDelayMs = ((useStep % 2) == 1 && swingAmount > 0.01f) ? swingAmount * 40.0f : 0.0f;
                    float totalDelaySamples = ((grooveOffsetMs + swingDelayMs) / 1000.0f) * args.sampleRate;

                    int bParam = r * 5;
                    float dm = params[TIMELINE_DECAY_PARAM + bParam].getValue();
                    if (inputs[TIMELINE_DECAY_CV_INPUT + r*4].isConnected()) { dm += inputs[TIMELINE_DECAY_CV_INPUT + r*4].getVoltage() * 0.18f; dm = clamp(dm, 0.2f, 2.0f); }
                    bool isStrongBeat = (useStep % 4 == 0);

                    WorldRhythm::Pattern& pp = fillActive ? fillPatterns.patterns[vb] : patterns.patterns[vb];
                    if (useStep < static_cast<int>(pp.length) && pp.hasOnsetAt(useStep)) {
                        float vel = pp.getVelocity(useStep) * groove.velMods[pos];
                        if (vel < 0.0f) vel = 0.0f; if (vel > 1.0f) vel = 1.0f;
                        bool acc = pp.accents[useStep % pp.length];
                        if (totalDelaySamples > 1.0f) {
                            if (delayedTriggerCount < MAX_DELAYED_TRIGGERS) { DelayedTrigger& dt = delayedTriggers[delayedTriggerCount++]; dt.samplesRemaining = totalDelaySamples; dt.voice = vb; dt.velocity = vel; dt.isAccent = acc; dt.role = r; dt.isStrongBeat = isStrongBeat; dt.isSubNote = false; }
                        } else { triggerWithArticulation(vb, vel, acc, args.sampleRate, r, isStrongBeat); externalVCA[vb].trigger(200.0f * dm, args.sampleRate, vel); }
                    }

                    WorldRhythm::Pattern& sp = fillActive ? fillPatterns.patterns[vb+1] : patterns.patterns[vb+1];
                    if (useStep < static_cast<int>(sp.length) && sp.hasOnsetAt(useStep)) {
                        float vel = sp.getVelocity(useStep) * groove.velMods[pos];
                        if (vel < 0.0f) vel = 0.0f; if (vel > 1.0f) vel = 1.0f;
                        bool acc = sp.accents[useStep % sp.length];
                        if (totalDelaySamples > 1.0f) {
                            if (delayedTriggerCount < MAX_DELAYED_TRIGGERS) { DelayedTrigger& dt = delayedTriggers[delayedTriggerCount++]; dt.samplesRemaining = totalDelaySamples; dt.voice = vb+1; dt.velocity = vel; dt.isAccent = acc; dt.role = r; dt.isStrongBeat = isStrongBeat; dt.isSubNote = false; }
                        } else { triggerWithArticulation(vb+1, vel, acc, args.sampleRate, r, isStrongBeat); externalVCA[vb+1].trigger(200.0f * dm, args.sampleRate, vel); }
                    }

                    currentSteps[r]++;
                    if (currentSteps[r] >= roleLengths[r]) currentSteps[r] = 0;
                }
                if (fillActive) { fillStepsRemaining--; if (fillStepsRemaining <= 0) { fillActive = false; currentFillType = WorldRhythm::FillType::NONE; } }
            }
            globalStep += stepsPerClock;
            if (globalStep >= maxLen) { globalStep = 0; currentBar++; }
        }

        // Audio processing
        float mixL = 0.0f, mixR = 0.0f;
        float spread = params[SPREAD_PARAM].getValue();
        const float rolePanV1[4] = {0.20f, 0.0f, -0.30f, -0.40f};
        const float rolePanV2[4] = {0.25f, 0.0f,  0.30f, -0.50f};

        for (int r = 0; r < 4; r++) {
            int vb = r * 2;
            float mix = params[TIMELINE_MIX_PARAM + r].getValue();
            currentMix[r] = mix;
            float panMerged = (rolePanV1[r] + rolePanV2[r]) * 0.5f * spread;

            float sa1 = drumSynth.processVoice(vb) * 5.0f;
            float ea1 = 0.0f;
            if (inputs[TIMELINE_AUDIO_INPUT_1 + r*2].isConnected()) {
                float ext = inputs[TIMELINE_AUDIO_INPUT_1 + r*2].getVoltage();
                ea1 = ext * externalVCA[vb].process() * currentVelocities[vb];
                if (currentAccents[vb]) ea1 *= 1.5f;
            }
            float c1 = sa1 * (1.0f - mix) + ea1 * mix;

            float sa2 = drumSynth.processVoice(vb+1) * 5.0f;
            float ea2 = 0.0f;
            if (inputs[TIMELINE_AUDIO_INPUT_2 + r*2].isConnected()) {
                float ext = inputs[TIMELINE_AUDIO_INPUT_2 + r*2].getVoltage();
                ea2 = ext * externalVCA[vb+1].process() * currentVelocities[vb+1];
                if (currentAccents[vb+1]) ea2 *= 1.5f;
            }
            float c2 = sa2 * (1.0f - mix) + ea2 * mix;

            float merged = lastTriggerWasPrimary[r] ? c1 : c2;
            outputs[TIMELINE_AUDIO_OUTPUT + r * 4].setVoltage(merged);

            float gL = 0.5f * (1.0f - panMerged);
            float gR = 0.5f * (1.0f + panMerged);
            mixL += merged * gL;
            mixR += merged * gR;
        }

        isolator.process(mixL, mixR, params[ISO_LOW_PARAM].getValue(), params[ISO_MID_PARAM].getValue(), params[ISO_HIGH_PARAM].getValue());
        tubeDrive.process(mixL, mixR, params[DRIVE_PARAM].getValue());

        outputs[MIX_L_OUTPUT].setVoltage(std::tanh(mixL) * 5.0f);
        outputs[MIX_R_OUTPUT].setVoltage(std::tanh(mixR) * 5.0f);

        bool clockGate = clockPulse.process(args.sampleTime);
        lights[CLOCK_LIGHT].setBrightness(clockGate ? 1.0f : 0.0f);

        for (int r = 0; r < 4; r++) {
            bool gate = gatePulses[r].process(args.sampleTime);
            outputs[TIMELINE_GATE_OUTPUT + r*4].setVoltage(gate ? 10.0f : 0.0f);
            outputs[TIMELINE_PITCH_OUTPUT + r*4].setVoltage(currentPitches[r]);
            outputs[TIMELINE_VELENV_OUTPUT + r*4].setVoltage(velocityEnv[r].process(args.sampleTime));
            lights[TIMELINE_LIGHT + r].setBrightness(gate ? 1.0f : 0.0f);
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "currentBar", json_integer(currentBar));
        json_object_set_new(rootJ, "ppqn", json_integer(ppqn));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* barJ = json_object_get(rootJ, "currentBar");
        if (barJ) currentBar = json_integer_value(barJ);
        json_t* ppqnJ = json_object_get(rootJ, "ppqn");
        if (ppqnJ) ppqn = json_integer_value(ppqnJ);
    }
};

// ============================================================================
// Module Widget - 32HP (MetaModule)
// ============================================================================

struct UniRhythmWidget : ModuleWidget {
    UniRhythmWidget(UniRhythm* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "UniRhythm.png")));
        box.size = Vec(32 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        float ctrlY = 120;
        float ctrlSpacing = 38;

        addInput(createInputCentered<PJ301MPort>(Vec(25, ctrlY + 5), module, UniRhythm::CLOCK_INPUT));
        addChild(createLightCentered<SmallLight<YellowLight>>(Vec(37, ctrlY - 2), module, UniRhythm::CLOCK_LIGHT));

        addParam(createParamCentered<VCVButton>(Vec(25 + ctrlSpacing, ctrlY + 5), module, UniRhythm::RESET_BUTTON_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(48 + ctrlSpacing, ctrlY + 5), module, UniRhythm::RESET_INPUT));

        addParam(createParamCentered<VCVButton>(Vec(25 + ctrlSpacing * 2 + 10, ctrlY + 5), module, UniRhythm::REGENERATE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(48 + ctrlSpacing * 2 + 10, ctrlY + 5), module, UniRhythm::REGENERATE_INPUT));

        float globalX = 175;
        float globalSpacing = 35;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(globalX, ctrlY + 5), module, UniRhythm::VARIATION_PARAM));
        globalX += globalSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(globalX, ctrlY + 5), module, UniRhythm::HUMANIZE_PARAM));
        globalX += globalSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(globalX, ctrlY + 5), module, UniRhythm::SWING_PARAM));
        globalX += globalSpacing;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(globalX, ctrlY + 5), module, UniRhythm::REST_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(globalX + 25, ctrlY + 5), module, UniRhythm::REST_CV_INPUT));

        float fillX = globalX + 60;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(fillX, ctrlY + 5), module, UniRhythm::FILL_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(fillX + 25, ctrlY + 5), module, UniRhythm::FILL_INPUT));

        float artX = fillX + 60;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(artX, ctrlY + 5), module, UniRhythm::ARTICULATION_PARAM));
        artX += 43;
        addParam(createParamCentered<RoundBlackKnob>(Vec(artX, ctrlY + 5), module, UniRhythm::SPREAD_PARAM));

        // Per-Role Section
        float roleY = 180;
        float roleSpacing = 121.92f;
        float roleStartX = 60.96f;
        float knobVSpacing = 49;
        float labelToKnob = 25;
        const int uiToRole[4] = {1, 0, 2, 3};

        for (int uiPos = 0; uiPos < 4; uiPos++) {
            int role = uiToRole[uiPos];
            float x = roleStartX + uiPos * roleSpacing;
            int baseParam = role * 5;
            float leftCol = x - 42;

            addParam(createParamCentered<RoundBlackKnob>(Vec(leftCol, roleY + 8 + labelToKnob), module, UniRhythm::TIMELINE_STYLE_PARAM + baseParam));
            addInput(createInputCentered<PJ301MPort>(Vec(leftCol + 26, roleY + 8 + labelToKnob), module, UniRhythm::TIMELINE_STYLE_CV_INPUT + role * 4));
            addParam(createParamCentered<RoundBlackKnob>(Vec(leftCol, roleY + 5 + knobVSpacing + labelToKnob), module, UniRhythm::TIMELINE_DENSITY_PARAM + baseParam));
            addInput(createInputCentered<PJ301MPort>(Vec(leftCol + 26, roleY + 5 + knobVSpacing + labelToKnob), module, UniRhythm::TIMELINE_DENSITY_CV_INPUT + role * 4));
            addParam(createParamCentered<RoundBlackKnob>(Vec(leftCol, roleY + 2 + knobVSpacing * 2 + labelToKnob), module, UniRhythm::TIMELINE_LENGTH_PARAM + baseParam));

            float rightCol = x + 12;
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightCol, roleY + 8 + labelToKnob), module, UniRhythm::TIMELINE_FREQ_PARAM + baseParam));
            addInput(createInputCentered<PJ301MPort>(Vec(rightCol + 26, roleY + 8 + labelToKnob), module, UniRhythm::TIMELINE_FREQ_CV_INPUT + role * 4));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightCol, roleY + 5 + knobVSpacing + labelToKnob), module, UniRhythm::TIMELINE_DECAY_PARAM + baseParam));
            addInput(createInputCentered<PJ301MPort>(Vec(rightCol + 26, roleY + 5 + knobVSpacing + labelToKnob), module, UniRhythm::TIMELINE_DECAY_CV_INPUT + role * 4));

            float row3ElementY = roleY + 2 + knobVSpacing * 2 + labelToKnob;
            addInput(createInputCentered<PJ301MPort>(Vec(rightCol + 26, row3ElementY), module, UniRhythm::TIMELINE_AUDIO_INPUT_1 + role * 2));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightCol, row3ElementY), module, UniRhythm::TIMELINE_MIX_PARAM + role));
        }

        // Output Area
        float row1Y = 343;
        float row2Y = 368;
        float roleOutputSpacing = 95.f;
        float roleOutputStartX = 65.f;
        const int roleUIToActual[4] = {1, 0, 2, 3};

        for (int i = 0; i < 4; i++) {
            float centerX = roleOutputStartX + i * roleOutputSpacing;
            int role = roleUIToActual[i];
            addOutput(createOutputCentered<PJ301MPort>(Vec(centerX - 14, row1Y), module, UniRhythm::TIMELINE_AUDIO_OUTPUT + role * 4));
            addOutput(createOutputCentered<PJ301MPort>(Vec(centerX + 14, row1Y), module, UniRhythm::TIMELINE_GATE_OUTPUT + role * 4));
            addOutput(createOutputCentered<PJ301MPort>(Vec(centerX - 14, row2Y), module, UniRhythm::TIMELINE_PITCH_OUTPUT + role * 4));
            addOutput(createOutputCentered<PJ301MPort>(Vec(centerX + 14, row2Y), module, UniRhythm::TIMELINE_VELENV_OUTPUT + role * 4));
        }

        float mixOutputX = 438.72f;
        addOutput(createOutputCentered<PJ301MPort>(Vec(mixOutputX, row1Y), module, UniRhythm::MIX_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(mixOutputX, row2Y), module, UniRhythm::MIX_R_OUTPUT));

        // Master Isolator + Drive knobs
        float isoKnobY = 355.5f;
        for (int i = 0; i < 4; i++) {
            float leftCenter = roleOutputStartX + i * roleOutputSpacing;
            float rightCenter = (i < 3) ? (roleOutputStartX + (i + 1) * roleOutputSpacing) : mixOutputX;
            float gX = (leftCenter + rightCenter) / 2.f;
            const int isoParams[4] = {UniRhythm::ISO_LOW_PARAM, UniRhythm::ISO_MID_PARAM, UniRhythm::ISO_HIGH_PARAM, UniRhythm::DRIVE_PARAM};
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(gX, isoKnobY), module, isoParams[i]));
        }
    }
};

Model* modelUniRhythm = createModel<UniRhythm, UniRhythmWidget>("UniRhythm");
