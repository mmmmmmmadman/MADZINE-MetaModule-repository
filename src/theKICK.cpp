#include "plugin.hpp"
#include <cmath>
#include <cstring>
#include "filesystem/async_filebrowser.hh"
#include "wav/dr_wav.h"

struct theKICK : Module {
    enum ParamId {
        PITCH_PARAM,
        SWEEP_PARAM,
        BEND_PARAM,
        DECAY_PARAM,
        FOLD_PARAM,
        SAMPLE_PARAM,
        FB_PARAM,
        TONE_PARAM,
        MODE_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        TRIGGER_INPUT,
        PITCH_CV_INPUT,
        SWEEP_CV_INPUT,
        BEND_CV_INPUT,
        DECAY_CV_INPUT,
        FOLD_CV_INPUT,
        FB_CV_INPUT,
        TONE_CV_INPUT,
        SAMPLE_CV_INPUT,
        ACCENT_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        MODE_LIGHT_RED,
        MODE_LIGHT_GREEN,
        MODE_LIGHT_BLUE,
        LIGHTS_LEN
    };

    // --- DSP state ---
    float phase = 0.f;
    float pitchEnvTime = 0.f;
    float ampEnvTime = 0.f;
    bool active = false;
    dsp::SchmittTrigger triggerDetect;

    // Feedback FM state
    float fbY1 = 0.f;
    float fbY2 = 0.f;

    // Accent (sampled on trigger, TR-808/909 style)
    // 0V -> non-accented, 10V -> fully accented; affects volume/sweep/drive
    float accentVolume = 1.f;      // 0.5 -> 1.0 (+6 dB)
    float accentSweepMult = 1.f;   // 1.0x -> 1.8x (pitch env depth)
    float accentDriveMult = 1.f;   // 1.0x -> 1.26x (+2 dB pre-saturation gain)

    // LPF state (4-pole, 24dB/oct)
    float lpfState[4] = {};

    // Sample FM playback position
    float samplePlayPos = 0.f;

    // --- Sample-as-Transfer ---
    static constexpr int TABLE_SIZE = 1024;
    float sampleTable[TABLE_SIZE] = {};
    bool hasSample = false;
    char samplePath[256] = {};

    // --- Mode (sample interaction type) ---
    // 0=PM(amber), 1=RM(rose), 2=AM(green), 3=SYNC(blue)
    int modeValue = 0;
    float prevSampleVal = 0.f;  // for SYNC zero-crossing detection
    dsp::SchmittTrigger modeTrigger;

    // --- CV modulation display ---
    float pitchCvMod = 0.f;
    float sweepCvMod = 0.f;
    float bendCvMod = 0.f;
    float decayCvMod = 0.f;
    float foldCvMod = 0.f;
    float sampleCvMod = 0.f;
    float fbCvMod = 0.f;
    float toneCvMod = 0.f;

    // --- Cached params for processSingleSample ---
    struct ProcessState {
        float pitch, sweep, bend, decayMs, fold, sampleFm, fb, toneCutoff;
    };

    // ========================================================================
    // Constructor
    // ========================================================================

    theKICK() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(PITCH_PARAM, 20.f, 200.f, 47.f, "Pitch", " Hz");
        configParam(SWEEP_PARAM, 0.f, 500.f, 260.f, "Sweep", " Hz");
        configParam(BEND_PARAM, 0.5f, 4.f, 0.88f, "Bend");
        configParam(DECAY_PARAM, 10.f, 1000.f, 136.f, "Decay", " ms");
        configParam(FOLD_PARAM, 0.f, 10.f, 0.3f, "Fold");
        configParam(SAMPLE_PARAM, 0.f, 10.f, 0.f, "Sample");
        configParam(FB_PARAM, 0.f, 1.f, 0.f, "Feedback");
        configParam(TONE_PARAM, 0.f, 10.f, 10.f, "Tone");
        configParam(MODE_PARAM, 0.f, 3.f, 0.f, "FM Mode");
        getParamQuantity(MODE_PARAM)->snapEnabled = true;

        configInput(TRIGGER_INPUT, "Trigger");
        configInput(PITCH_CV_INPUT, "Pitch CV (V/Oct)");
        configInput(SWEEP_CV_INPUT, "Sweep CV");
        configInput(BEND_CV_INPUT, "Bend CV");
        configInput(DECAY_CV_INPUT, "Decay CV");
        configInput(FOLD_CV_INPUT, "Fold CV");
        configInput(FB_CV_INPUT, "Feedback CV");
        configInput(TONE_CV_INPUT, "Tone CV");
        configInput(SAMPLE_CV_INPUT, "Sample CV");
        configInput(ACCENT_INPUT, "Accent");

        configOutput(OUT_OUTPUT, "Kick Output");

        configLight(MODE_LIGHT_RED, "Mode Red");
        configLight(MODE_LIGHT_GREEN, "Mode Green");
        configLight(MODE_LIGHT_BLUE, "Mode Blue");
    }

    void onReset() override {
        phase = 0.f;
        pitchEnvTime = 0.f;
        ampEnvTime = 0.f;
        active = false;
        fbY1 = 0.f;
        fbY2 = 0.f;
        accentVolume = 1.f;
        accentSweepMult = 1.f;
        accentDriveMult = 1.f;
        for (int i = 0; i < 4; i++) lpfState[i] = 0.f;
        samplePlayPos = 0.f;
        modeValue = 0;
        hasSample = false;
        for (int i = 0; i < TABLE_SIZE; i++) sampleTable[i] = 0.f;
    }

    // ========================================================================
    // Sample loading
    // ========================================================================

    void loadSampleFromFile() {
        async_open_file("", "wav,WAV", "Load Sample",
            [this](char* path) {
                if (path) {
                    loadSampleTable(path);
                    free(path);
                }
            }
        );
    }

    void loadSampleTable(const char* path) {
        drwav wav;
        if (!drwav_init_file(&wav, path, NULL)) {
            return;
        }

        unsigned int totalFrames = (unsigned int)wav.totalPCMFrameCount;
        unsigned int channels = wav.channels;

        if (totalFrames == 0) {
            drwav_uninit(&wav);
            return;
        }

        float* rawData = (float*)malloc(totalFrames * channels * sizeof(float));
        if (!rawData) {
            drwav_uninit(&wav);
            return;
        }

        drwav_uint64 framesRead = drwav_read_pcm_frames_f32(&wav, totalFrames, rawData);
        drwav_uninit(&wav);

        if (framesRead == 0) {
            free(rawData);
            return;
        }

        float peak = 0.f;
        for (drwav_uint64 i = 0; i < framesRead; i++) {
            float s = rawData[i * channels];
            float a = s < 0.f ? -s : s;
            if (a > peak) peak = a;
        }
        if (peak < 0.0001f) peak = 1.f;

        for (int i = 0; i < TABLE_SIZE; i++) {
            float pos = (float)i / (float)(TABLE_SIZE - 1) * (float)(framesRead - 1);
            int idx = (int)pos;
            float frac = pos - idx;
            int next = idx + 1;
            if (next >= (int)framesRead) next = (int)framesRead - 1;
            sampleTable[i] = (rawData[idx * channels] * (1.f - frac) + rawData[next * channels] * frac) / peak;
        }

        free(rawData);
        hasSample = true;

        strncpy(samplePath, path, sizeof(samplePath) - 1);
        samplePath[sizeof(samplePath) - 1] = '\0';
    }

    void clearSample() {
        hasSample = false;
        samplePath[0] = '\0';
        for (int i = 0; i < TABLE_SIZE; i++) sampleTable[i] = 0.f;
    }

    // ========================================================================
    // Waveshaper functions
    // ========================================================================

    // Lookup sample transfer table with linear interpolation
    float lookupSampleTable(float x) {
        // x in [-1, 1] -> index in [0, TABLE_SIZE-1]
        float normalized = (x + 1.f) * 0.5f;
        float pos = normalized * (TABLE_SIZE - 1);
        int idx = clamp((int)pos, 0, TABLE_SIZE - 2);
        float frac = pos - idx;
        return sampleTable[idx] * (1.f - frac) + sampleTable[idx + 1] * frac;
    }

    // ========================================================================
    // Single sample DSP (called at sample rate)
    // ========================================================================

    float processSingleSample(const ProcessState& state, float sampleTime) {
        if (!active) return 0.f;

        // Pitch envelope: freq = pitch + sweep * exp(-t / (0.015 / bend))
        // Accent boosts sweep depth up to 1.8x for snappier attack
        float pitchTau = 0.015f / state.bend;
        float pitchEnv = state.sweep * accentSweepMult * std::exp(-pitchEnvTime / pitchTau);
        float freq = state.pitch + pitchEnv;

        // Self-feedback PM
        float fbPhase = 0.f;
        if (state.fb > 0.001f) {
            fbPhase = state.fb * 0.5f * (fbY1 + fbY2);
        }

        // Read sample value (needed for all modes)
        float sampleVal = 0.f;
        float modDepth = 0.f;
        float sampleEnv = 0.f;
        bool useSample = hasSample && state.sampleFm > 0.01f;
        if (useSample) {
            float tablePos = samplePlayPos * TABLE_SIZE;
            int idx = ((int)tablePos) % TABLE_SIZE;
            if (idx < 0) idx += TABLE_SIZE;
            int next = (idx + 1) % TABLE_SIZE;
            float frac = tablePos - std::floor(tablePos);
            sampleVal = sampleTable[idx] * (1.f - frac) + sampleTable[next] * frac;
            modDepth = state.sampleFm / 10.f;  // 0~1 normalized
            sampleEnv = std::exp(-pitchEnvTime / pitchTau);

            // Advance sample playback at oscillator frequency
            samplePlayPos += freq * sampleTime;
            while (samplePlayPos >= 1.f) samplePlayPos -= 1.f;
        }

        // Phase accumulator
        phase += freq * sampleTime;
        while (phase >= 1.f) phase -= 1.f;
        while (phase < 0.f) phase += 1.f;

        // Mode-dependent oscillator: sample interaction type
        float osc;
        if (useSample) {
            float carrier = std::sin(2.f * M_PI * phase + fbPhase);
            switch (modeValue) {
                case 0: { // PM: phase modulation (classic FM)
                    float fmIndex = modDepth * 4.f * M_PI;  // 0~4pi
                    float samplePhase = fmIndex * sampleVal * sampleEnv;
                    osc = std::sin(2.f * M_PI * phase + fbPhase + samplePhase);
                    break;
                }
                case 1: { // RM: ring modulation
                    float depth = modDepth * sampleEnv;
                    osc = carrier * (1.f - depth + depth * sampleVal);
                    break;
                }
                case 2: { // AM: amplitude modulation
                    float depth = modDepth * sampleEnv;
                    osc = carrier * (1.f + depth * sampleVal);
                    break;
                }
                case 3: { // SYNC: hard sync (phase reset on zero crossings)
                    float depth = modDepth * sampleEnv;
                    if (prevSampleVal * sampleVal < 0.f && depth > 0.01f) {
                        phase *= (1.f - depth);
                    }
                    osc = std::sin(2.f * M_PI * phase + fbPhase);
                    break;
                }
                default:
                    osc = carrier;
                    break;
            }
            prevSampleVal = sampleVal;
        } else {
            osc = std::sin(2.f * M_PI * phase + fbPhase);
        }

        // Update feedback state
        fbY2 = fbY1;
        fbY1 = osc;

        // Tone LPF (4-pole, 24dB/oct cascaded one-pole with frequency warping)
        float fc = state.toneCutoff * sampleTime;
        fc = clamp(fc, 0.0001f, 0.4999f);
        float wc = std::tan(M_PI * fc);
        float lpAlpha = wc / (1.f + wc);
        lpfState[0] = osc * lpAlpha + lpfState[0] * (1.f - lpAlpha);
        lpfState[1] = lpfState[0] * lpAlpha + lpfState[1] * (1.f - lpAlpha);
        lpfState[2] = lpfState[1] * lpAlpha + lpfState[2] * (1.f - lpAlpha);
        lpfState[3] = lpfState[2] * lpAlpha + lpfState[3] * (1.f - lpAlpha);
        float filtered = lpfState[3];

        // Post-LPF Drive: tanh saturation
        // Accent adds +2 dB pre-saturation gain for more harmonic punch
        if (state.fold > 0.01f) {
            float g = (1.f + state.fold * 0.5f) * accentDriveMult;  // 1~6x gain, accent boosts
            float tanhG = std::tanh(g);
            filtered = std::tanh(filtered * g) / tanhG;
        }

        // Amplitude envelope: simple exponential decay
        float decaySec = state.decayMs * 0.001f;
        float ampEnv = std::exp(-ampEnvTime / decaySec);

        // Output
        float output = filtered * ampEnv * 8.f;

        // Advance envelope times
        pitchEnvTime += sampleTime;
        ampEnvTime += sampleTime;

        // Deactivate when silent
        if (ampEnv < 0.001f) {
            active = false;
        }

        return output;
    }

    // ========================================================================
    // Main process
    // ========================================================================

    void process(const ProcessArgs& args) override {
        // Read parameters
        float pitch = params[PITCH_PARAM].getValue();
        float sweep = params[SWEEP_PARAM].getValue();
        float bend = params[BEND_PARAM].getValue();
        float decayMs = params[DECAY_PARAM].getValue();
        float fold = params[FOLD_PARAM].getValue();
        float sampleMix = params[SAMPLE_PARAM].getValue();
        float fb = params[FB_PARAM].getValue();
        float toneKnob = params[TONE_PARAM].getValue();

        // Apply CV modulation
        if (inputs[PITCH_CV_INPUT].isConnected()) {
            float cv = inputs[PITCH_CV_INPUT].getVoltage();
            pitch *= std::pow(2.f, cv);
            pitchCvMod = clamp(cv / 5.f, -1.f, 1.f);
        } else { pitchCvMod = 0.f; }

        if (inputs[SWEEP_CV_INPUT].isConnected()) {
            float cv = inputs[SWEEP_CV_INPUT].getVoltage();
            sweep += cv * 50.f;
            sweep = clamp(sweep, 0.f, 1000.f);
            sweepCvMod = clamp(cv / 5.f, -1.f, 1.f);
        } else { sweepCvMod = 0.f; }

        if (inputs[BEND_CV_INPUT].isConnected()) {
            float cv = inputs[BEND_CV_INPUT].getVoltage();
            bend += cv * 0.35f;
            bend = clamp(bend, 0.5f, 4.f);
            bendCvMod = clamp(cv / 5.f, -1.f, 1.f);
        } else { bendCvMod = 0.f; }

        if (inputs[DECAY_CV_INPUT].isConnected()) {
            float cv = inputs[DECAY_CV_INPUT].getVoltage();
            decayMs += cv * 100.f;
            decayMs = clamp(decayMs, 10.f, 2000.f);
            decayCvMod = clamp(cv / 5.f, -1.f, 1.f);
        } else { decayCvMod = 0.f; }

        if (inputs[FOLD_CV_INPUT].isConnected()) {
            float cv = inputs[FOLD_CV_INPUT].getVoltage();
            fold += cv;
            fold = clamp(fold, 0.f, 10.f);
            foldCvMod = clamp(cv / 5.f, -1.f, 1.f);
        } else { foldCvMod = 0.f; }

        if (inputs[SAMPLE_CV_INPUT].isConnected()) {
            float cv = inputs[SAMPLE_CV_INPUT].getVoltage();
            sampleMix += cv;
            sampleMix = clamp(sampleMix, 0.f, 10.f);
            sampleCvMod = clamp(cv / 5.f, -1.f, 1.f);
        } else { sampleCvMod = 0.f; }

        if (inputs[FB_CV_INPUT].isConnected()) {
            float cv = inputs[FB_CV_INPUT].getVoltage();
            fb += cv * 0.1f;
            fb = clamp(fb, 0.f, 1.f);
            fbCvMod = clamp(cv / 5.f, -1.f, 1.f);
        } else { fbCvMod = 0.f; }

        if (inputs[TONE_CV_INPUT].isConnected()) {
            float cv = inputs[TONE_CV_INPUT].getVoltage();
            toneKnob += cv;
            toneKnob = clamp(toneKnob, 0.f, 10.f);
            toneCvMod = clamp(cv / 5.f, -1.f, 1.f);
        } else { toneCvMod = 0.f; }

        // Tone knob to frequency: 0=40Hz, 10=20kHz (logarithmic)
        float toneCutoff = 40.f * std::pow(500.f, toneKnob / 10.f);

        // Mode LED: show current mode when sample is loaded
        if (hasSample) {
            modeValue = (int)params[MODE_PARAM].getValue();
            switch (modeValue) {
                case 0: // PM (amber)
                    lights[MODE_LIGHT_RED].setBrightness(1.f);
                    lights[MODE_LIGHT_GREEN].setBrightness(0.6f);
                    lights[MODE_LIGHT_BLUE].setBrightness(0.f);
                    break;
                case 1: // RM (rose)
                    lights[MODE_LIGHT_RED].setBrightness(1.f);
                    lights[MODE_LIGHT_GREEN].setBrightness(0.f);
                    lights[MODE_LIGHT_BLUE].setBrightness(0.3f);
                    break;
                case 2: // AM (green)
                    lights[MODE_LIGHT_RED].setBrightness(0.f);
                    lights[MODE_LIGHT_GREEN].setBrightness(1.f);
                    lights[MODE_LIGHT_BLUE].setBrightness(0.f);
                    break;
                case 3: // SYNC (blue)
                    lights[MODE_LIGHT_RED].setBrightness(0.f);
                    lights[MODE_LIGHT_GREEN].setBrightness(0.f);
                    lights[MODE_LIGHT_BLUE].setBrightness(1.f);
                    break;
            }
        } else {
            lights[MODE_LIGHT_RED].setBrightness(0.f);
            lights[MODE_LIGHT_GREEN].setBrightness(0.f);
            lights[MODE_LIGHT_BLUE].setBrightness(0.f);
        }

        // Trigger detection
        if (triggerDetect.process(inputs[TRIGGER_INPUT].getVoltage(), 0.1f, 2.f)) {
            phase = 0.f;
            pitchEnvTime = 0.f;
            ampEnvTime = 0.f;
            fbY1 = 0.f;
            fbY2 = 0.f;
            prevSampleVal = 0.f;
            for (int i = 0; i < 4; i++) lpfState[i] = 0.f;
            samplePlayPos = 0.f;
            active = true;

            // Sample accent on trigger (TR-808/909 style: multi-parameter)
            // 0V -> non-accented baseline, 10V -> fully accented
            // Affects: volume (+6 dB), pitch sweep depth (+80%), drive (+2 dB)
            if (inputs[ACCENT_INPUT].isConnected()) {
                float cv = clamp(inputs[ACCENT_INPUT].getVoltage() / 10.f, 0.f, 1.f);
                accentVolume = 0.5f + cv * 0.5f;      // 0.5 -> 1.0 (6 dB range)
                accentSweepMult = 1.f + cv * 0.8f;    // 1.0x -> 1.8x
                accentDriveMult = 1.f + cv * 0.26f;   // 1.0x -> 1.26x (+2 dB)
            } else {
                accentVolume = 1.f;
                accentSweepMult = 1.f;
                accentDriveMult = 1.f;
            }
        }

        // Build process state
        ProcessState state;
        state.pitch = pitch;
        state.sweep = sweep;
        state.bend = bend;
        state.decayMs = decayMs;
        state.fold = fold;
        state.sampleFm = sampleMix;
        state.fb = fb;
        state.toneCutoff = toneCutoff;

        // Process single sample directly (no oversampling)
        float outputFinal = processSingleSample(state, args.sampleTime);

        outputs[OUT_OUTPUT].setVoltage(outputFinal * accentVolume);
    }

    // ========================================================================
    // JSON serialization
    // ========================================================================

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "modeValue", json_integer(modeValue));
        if (hasSample) {
            json_object_set_new(rootJ, "hasSample", json_true());
            if (samplePath[0] != '\0')
                json_object_set_new(rootJ, "samplePath", json_string(samplePath));
            json_t* tableJ = json_array();
            for (int i = 0; i < TABLE_SIZE; i++) {
                json_array_append_new(tableJ, json_real(sampleTable[i]));
            }
            json_object_set_new(rootJ, "sampleTable", tableJ);
        }
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* modeJ = json_object_get(rootJ, "modeValue");
        if (modeJ) {
            modeValue = json_integer_value(modeJ);
            params[MODE_PARAM].setValue((float)modeValue);
        }
        json_t* hasSampleJ = json_object_get(rootJ, "hasSample");
        if (hasSampleJ && json_is_true(hasSampleJ)) {
            json_t* tableJ = json_object_get(rootJ, "sampleTable");
            if (tableJ && json_is_array(tableJ)) {
                int len = (int)json_array_size(tableJ);
                if (len > TABLE_SIZE) len = TABLE_SIZE;
                for (int i = 0; i < len; i++) {
                    sampleTable[i] = json_number_value(json_array_get(tableJ, i));
                }
                hasSample = true;
            }
            json_t* pathJ = json_object_get(rootJ, "samplePath");
            if (pathJ) {
                const char* p = json_string_value(pathJ);
                if (p) {
                    strncpy(samplePath, p, sizeof(samplePath) - 1);
                    samplePath[sizeof(samplePath) - 1] = '\0';
                }
            }
        }
    }
};

// ============================================================================
// Widget
// ============================================================================

struct theKICKWidget : ModuleWidget {
    theKICKWidget(theKICK* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "theKICK.png")));

        float panelWidth = 121.92f; // 8HP

        // ================================================================
        // Layout constants
        // ================================================================
        float colL = 32.f;
        float colR = 90.f;

        // Row 4 three-column X positions
        float col4L = 24.f;
        float col4M = 61.f;
        float col4R = 98.f;

        // Vertical positions
        float row1Y = 60.f;
        float row2Y = 135.f;
        float row3Y = 210.f;
        float row4Y = 288.f;

        float cvOffset = 28.f;

        // Output area
        float ioY = 356.f;

        // ================================================================
        // Knobs and CV ports
        // ================================================================

        // --- Row 1: PITCH ---
        addParam(createParamCentered<RoundBlackKnob>(Vec(colL, row1Y), module, theKICK::PITCH_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(colL, row1Y + cvOffset), module, theKICK::PITCH_CV_INPUT));

        // --- Row 1 right: MODE button + LED ---
        addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(Vec(colR, 86.f), module, theKICK::MODE_LIGHT_RED));
        addParam(createParamCentered<VCVButton>(Vec(colR, 86.f), module, theKICK::MODE_PARAM));

        // --- Row 2: SWEEP (left) + SAMPLE/FM (right) ---
        addParam(createParamCentered<RoundBlackKnob>(Vec(colL, row2Y), module, theKICK::SWEEP_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(colL, row2Y + cvOffset), module, theKICK::SWEEP_CV_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(Vec(colR, row2Y), module, theKICK::SAMPLE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(colR, row2Y + cvOffset), module, theKICK::SAMPLE_CV_INPUT));

        // --- Row 3: BEND (left) + FEEDBACK (right) ---
        addParam(createParamCentered<RoundBlackKnob>(Vec(colL, row3Y), module, theKICK::BEND_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(colL, row3Y + cvOffset), module, theKICK::BEND_CV_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(Vec(colR, row3Y), module, theKICK::FB_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(colR, row3Y + cvOffset), module, theKICK::FB_CV_INPUT));

        // --- Row 4: DECAY (left) + TONE (center) + DRIVE (right) ---
        addParam(createParamCentered<RoundBlackKnob>(Vec(col4L, row4Y), module, theKICK::DECAY_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col4L, row4Y + cvOffset), module, theKICK::DECAY_CV_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(Vec(col4M, row4Y), module, theKICK::TONE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col4M, row4Y + cvOffset), module, theKICK::TONE_CV_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(Vec(col4R, row4Y), module, theKICK::FOLD_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col4R, row4Y + cvOffset), module, theKICK::FOLD_CV_INPUT));

        // --- I/O in output area ---
        float ioLeft = 22.f;
        float ioCenter = panelWidth / 2.f;
        float ioRight = panelWidth - 22.f;

        addInput(createInputCentered<PJ301MPort>(Vec(ioLeft, ioY), module, theKICK::TRIGGER_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(ioCenter, ioY), module, theKICK::ACCENT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(ioRight, ioY), module, theKICK::OUT_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        theKICK* module = dynamic_cast<theKICK*>(this->module);
        if (!module) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Load Sample", "", [=]() {
            module->loadSampleFromFile();
        }));
        if (module->hasSample) {
            menu->addChild(createMenuItem("Clear Sample", "", [=]() {
                module->clearSample();
            }));
        }
    }
};

Model* modeltheKICK = createModel<theKICK, theKICKWidget>("theKICK");
