/**
 * Drummmmmmer.cpp
 * 4-Voice World Drum Synthesizer (MetaModule port)
 *
 * Ported from VCV Rack version
 * - Removed custom widgets (DrummmmmmerTextLabel, WDDynamicRoleTitle, DrummmmmmerWhitePanel, StyleDisplay)
 * - Removed panelTheme/panelContrast
 * - Removed WDStyleParamQuantity
 * - Uses standard knobs (RoundBlackKnob, RoundSmallBlackKnob)
 * - Uses PNG panel via createPanel
 */

#include "plugin.hpp"
#include "WorldRhythm/MinimalDrumSynth.hpp"

using namespace worldrhythm;

// ============================================================================
// DrummerSynth - 8 voice drum synth (2 voices per role)
// ============================================================================

class DrummerSynth {
private:
    MinimalVoice voices[8];
    float sampleRate = 44100.0f;

public:
    void setSampleRate(float sr) {
        sampleRate = sr;
        for (int i = 0; i < 8; i++) voices[i].setSampleRate(sr);
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

// ============================================================================
// 8-voice style presets (from UniRhythm ExtendedStylePreset)
// ============================================================================

struct DrummerStylePreset {
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

// Voice assignments: 0-1=Timeline, 2-3=Foundation, 4-5=Groove, 6-7=Lead
static const DrummerStylePreset DRUMMER_PRESETS[10] = {
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

inline void applyDrummerPreset(DrummerSynth& synth, int styleIndex) {
    if (styleIndex < 0 || styleIndex > 9) return;
    const DrummerStylePreset& preset = DRUMMER_PRESETS[styleIndex];
    for (int i = 0; i < 8; i++) {
        synth.setVoiceParams(i, preset.voices[i].mode,
                             preset.voices[i].freq, preset.voices[i].decay,
                             preset.voices[i].sweep, preset.voices[i].bend);
    }
}

// Style names
static const char* WD_STYLE_NAMES[10] = {
    "West African", "Afro-Cuban", "Brazilian", "Balkan", "Indian",
    "Gamelan", "Jazz", "Electronic", "Breakbeat", "Techno"
};

struct Drummmmmmer : Module {
    enum ParamId {
        STYLE_PARAM,
        SPREAD_PARAM,
        VOICE_PARAM,
        // Per-voice parameters (4 voices)
        FREQ_PARAM_TL,
        FREQ_PARAM_FD,
        FREQ_PARAM_GR,
        FREQ_PARAM_LD,
        DECAY_PARAM_TL,
        DECAY_PARAM_FD,
        DECAY_PARAM_GR,
        DECAY_PARAM_LD,
        PARAMS_LEN
    };

    enum InputId {
        STYLE_CV_INPUT,
        // Per-voice inputs (4 voices x 4 types)
        TRIG_INPUT_TL,
        TRIG_INPUT_FD,
        TRIG_INPUT_GR,
        TRIG_INPUT_LD,
        VEL_INPUT_TL,
        VEL_INPUT_FD,
        VEL_INPUT_GR,
        VEL_INPUT_LD,
        FREQ_CV_INPUT_TL,
        FREQ_CV_INPUT_FD,
        FREQ_CV_INPUT_GR,
        FREQ_CV_INPUT_LD,
        DECAY_CV_INPUT_TL,
        DECAY_CV_INPUT_FD,
        DECAY_CV_INPUT_GR,
        DECAY_CV_INPUT_LD,
        INPUTS_LEN
    };

    enum OutputId {
        // Per-voice outputs
        AUDIO_OUTPUT_TL,
        AUDIO_OUTPUT_FD,
        AUDIO_OUTPUT_GR,
        AUDIO_OUTPUT_LD,
        // Stereo mix
        MIX_L_OUTPUT,
        MIX_R_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightId {
        LIGHTS_LEN
    };

    // Drum synthesizer engine (8 voices: 2 per role)
    DrummerSynth drumSynth;

    // Voice selection RNG
    std::mt19937 voiceRng{std::random_device{}()};
    std::uniform_real_distribution<float> voiceDist{0.0f, 1.0f};

    // Trigger detection (Schmitt triggers)
    dsp::SchmittTrigger trigSchmitt[4];

    // Current style index
    int currentStyle = 0;

    // Last triggered voice index per role (0=v1, 1=v2)
    int lastTriggeredVoice[4] = {0, 0, 0, 0};

    Drummmmmmer() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Global parameters
        configParam(STYLE_PARAM, 0.f, 9.f, 0.f, "Style");
        getParamQuantity(STYLE_PARAM)->snapEnabled = true;
        configParam(SPREAD_PARAM, 0.f, 1.f, 0.5f, "Stereo Spread", "%", 0.f, 100.f);
        configParam(VOICE_PARAM, 0.f, 1.f, 0.f, "Voice Variation", "%", 0.f, 100.f);

        // Per-voice parameters
        const char* voiceNames[4] = {"Timeline", "Foundation", "Groove", "Lead"};

        for (int i = 0; i < 4; i++) {
            // FREQ: -1 to +1 (+-1 octave adjustment)
            configParam(FREQ_PARAM_TL + i, -1.f, 1.f, 0.f,
                        string::f("%s Freq", voiceNames[i]), " oct");
            // DECAY: 0.2 to 2.0 (multiplier)
            configParam(DECAY_PARAM_TL + i, 0.2f, 2.f, 1.f,
                        string::f("%s Decay", voiceNames[i]), "x");
        }

        // Inputs
        configInput(STYLE_CV_INPUT, "Style CV");

        for (int i = 0; i < 4; i++) {
            configInput(TRIG_INPUT_TL + i, string::f("%s Trigger", voiceNames[i]));
            configInput(VEL_INPUT_TL + i, string::f("%s Velocity CV", voiceNames[i]));
            configInput(FREQ_CV_INPUT_TL + i, string::f("%s Freq CV", voiceNames[i]));
            configInput(DECAY_CV_INPUT_TL + i, string::f("%s Decay CV", voiceNames[i]));
        }

        // Outputs
        for (int i = 0; i < 4; i++) {
            configOutput(AUDIO_OUTPUT_TL + i, string::f("%s Audio", voiceNames[i]));
        }
        configOutput(MIX_L_OUTPUT, "Mix L");
        configOutput(MIX_R_OUTPUT, "Mix R");

        // Initialize with default style
        applyDrummerPreset(drumSynth, 0);
    }

    void onSampleRateChange() override {
        drumSynth.setSampleRate(APP->engine->getSampleRate());
    }

    void process(const ProcessArgs& args) override {
        // Update sample rate
        drumSynth.setSampleRate(args.sampleRate);

        // Read style parameter with CV
        float styleValue = params[STYLE_PARAM].getValue();
        if (inputs[STYLE_CV_INPUT].isConnected()) {
            float cv = inputs[STYLE_CV_INPUT].getVoltage();
            styleValue += cv;
        }
        int newStyle = clamp((int)std::round(styleValue), 0, 9);

        // Apply style preset if changed
        if (newStyle != currentStyle) {
            currentStyle = newStyle;
            applyDrummerPreset(drumSynth, currentStyle);
        }

        // Get base preset for parameter modulation (8 voices)
        const DrummerStylePreset& preset = DRUMMER_PRESETS[currentStyle];

        // Voice variation probability
        float voiceProb = params[VOICE_PARAM].getValue();

        // Process each role (4 roles x 2 voices each)
        float voiceOutputs[4];

        for (int v = 0; v < 4; v++) {
            int v1 = v * 2;
            int v2 = v * 2 + 1;

            // Read parameters with CV modulation
            float freqParam = params[FREQ_PARAM_TL + v].getValue();
            float decayParam = params[DECAY_PARAM_TL + v].getValue();

            // FREQ CV: +-5V = +-1 octave
            if (inputs[FREQ_CV_INPUT_TL + v].isConnected()) {
                float cv = inputs[FREQ_CV_INPUT_TL + v].getVoltage();
                freqParam += cv * 0.2f;
            }
            freqParam = clamp(freqParam, -1.f, 1.f);

            // DECAY CV: +-5V = +-0.9 multiplier
            if (inputs[DECAY_CV_INPUT_TL + v].isConnected()) {
                float cv = inputs[DECAY_CV_INPUT_TL + v].getVoltage();
                decayParam += cv * 0.18f;
            }
            decayParam = clamp(decayParam, 0.2f, 2.f);

            // Apply modulation to both voices (each with its own base freq/decay)
            float modFreq1 = preset.voices[v1].freq * std::pow(2.f, freqParam);
            float modDecay1 = preset.voices[v1].decay * decayParam;
            float modFreq2 = preset.voices[v2].freq * std::pow(2.f, freqParam);
            float modDecay2 = preset.voices[v2].decay * decayParam;

            drumSynth.setVoiceParams(v1, preset.voices[v1].mode, modFreq1, modDecay1,
                                     preset.voices[v1].sweep, preset.voices[v1].bend);
            drumSynth.setVoiceParams(v2, preset.voices[v2].mode, modFreq2, modDecay2,
                                     preset.voices[v2].sweep, preset.voices[v2].bend);

            // Trigger detection - select v1 or v2 based on VOICE probability
            if (inputs[TRIG_INPUT_TL + v].isConnected()) {
                if (trigSchmitt[v].process(inputs[TRIG_INPUT_TL + v].getVoltage(), 0.1f, 2.f)) {
                    float velocity = 1.0f;
                    if (inputs[VEL_INPUT_TL + v].isConnected()) {
                        velocity = clamp(inputs[VEL_INPUT_TL + v].getVoltage() / 10.f, 0.f, 1.f);
                    }

                    bool useV2 = voiceDist(voiceRng) < voiceProb;
                    lastTriggeredVoice[v] = useV2 ? 1 : 0;
                    drumSynth.triggerVoice(useV2 ? v2 : v1, velocity);
                }
            }

            // Process both voices and sum (one decaying, one possibly fresh)
            voiceOutputs[v] = drumSynth.processVoice(v1) + drumSynth.processVoice(v2);
        }

        // Output per-voice audio
        for (int v = 0; v < 4; v++) {
            outputs[AUDIO_OUTPUT_TL + v].setVoltage(voiceOutputs[v] * 5.f);
        }

        // Stereo mix with spread
        float spread = params[SPREAD_PARAM].getValue();
        float mixL = 0.f, mixR = 0.f;

        // Panning positions: TL=-0.5, FD=0, GR=+0.3, LD=+0.7
        const float panPositions[4] = {-0.5f, 0.f, 0.3f, 0.7f};

        for (int v = 0; v < 4; v++) {
            float pan = panPositions[v] * spread;
            float gainL = std::cos((pan + 1.f) * 0.25f * M_PI);
            float gainR = std::sin((pan + 1.f) * 0.25f * M_PI);
            mixL += voiceOutputs[v] * gainL;
            mixR += voiceOutputs[v] * gainR;
        }

        // Soft limiting
        outputs[MIX_L_OUTPUT].setVoltage(std::tanh(mixL) * 5.f);
        outputs[MIX_R_OUTPUT].setVoltage(std::tanh(mixR) * 5.f);
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
    }
};

struct DrummmmmmerWidget : ModuleWidget {
    DrummmmmmerWidget(Drummmmmmer* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "Drummmmmmer.png")));

        box.size = Vec(8 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        // ========== GLOBAL CONTROLS ==========
        // STYLE knob (large) X=18, Y=56
        addParam(createParamCentered<RoundBlackKnob>(Vec(18.f, 56.f), module, Drummmmmmer::STYLE_PARAM));
        // STYLE CV port X=106, Y=56
        addInput(createInputCentered<PJ301MPort>(Vec(106.f, 56.f), module, Drummmmmmer::STYLE_CV_INPUT));

        // ========== 4 VOICES AREA ==========
        const float startY[4] = {98.f, 159.f, 220.f, 281.f};
        // Map UI row to internal voice index: Lead(3), Groove(2), Timeline(0), Foundation(1)
        const int voiceMap[4] = {3, 2, 0, 1};

        float trigX = 15.f;
        float freqX = 43.f;
        float decayX = 73.f;
        float outX = 103.f;

        for (int v = 0; v < 4; v++) {
            float sY = startY[v];
            int vi = voiceMap[v];

            // Row 1: TRIG port | FREQ knob (small) | DECAY knob (small) | Audio OUT port
            addInput(createInputCentered<PJ301MPort>(Vec(trigX, sY), module, Drummmmmmer::TRIG_INPUT_TL + vi));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(freqX, sY), module, Drummmmmmer::FREQ_PARAM_TL + vi));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(decayX, sY), module, Drummmmmmer::DECAY_PARAM_TL + vi));
            addOutput(createOutputCentered<PJ301MPort>(Vec(outX, sY), module, Drummmmmmer::AUDIO_OUTPUT_TL + vi));

            // Row 2 (sY+26): VEL port | FREQ CV port | DECAY CV port
            addInput(createInputCentered<PJ301MPort>(Vec(trigX, sY + 26.f), module, Drummmmmmer::VEL_INPUT_TL + vi));
            addInput(createInputCentered<PJ301MPort>(Vec(freqX, sY + 26.f), module, Drummmmmmer::FREQ_CV_INPUT_TL + vi));
            addInput(createInputCentered<PJ301MPort>(Vec(decayX, sY + 26.f), module, Drummmmmmer::DECAY_CV_INPUT_TL + vi));
        }

        // ========== WHITE OUTPUT AREA (Y=330-380) ==========
        // SPREAD knob (small) X=15, Y=355
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(15.f, 355.f), module, Drummmmmmer::SPREAD_PARAM));
        // VOICE knob (small) X=43, Y=355
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(43.f, 355.f), module, Drummmmmmer::VOICE_PARAM));
        // MIX L output X=73, Y=355
        addOutput(createOutputCentered<PJ301MPort>(Vec(73.f, 355.f), module, Drummmmmmer::MIX_L_OUTPUT));
        // MIX R output X=103, Y=355
        addOutput(createOutputCentered<PJ301MPort>(Vec(103.f, 355.f), module, Drummmmmmer::MIX_R_OUTPUT));
    }
};

Model* modelDrummmmmmer = createModel<Drummmmmmer, DrummmmmmerWidget>("Drummmmmmer");
