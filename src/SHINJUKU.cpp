#include "plugin.hpp"
#include <cmath>

struct ShinjukuBiquadPeakEQ {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;

    void setParams(float sampleRate, float freq, float gainDb, float Q = 1.41f) {
        float A = std::pow(10.f, gainDb / 40.f);
        float w0 = 2.f * M_PI * freq / sampleRate;
        float cosw0 = std::cos(w0); float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.f * Q);
        float a0 = 1.f + alpha / A;
        b0 = (1.f + alpha * A) / a0; b1 = (-2.f * cosw0) / a0;
        b2 = (1.f - alpha * A) / a0; a1 = b1; a2 = (1.f - alpha / A) / a0;
    }
    float process(float in) {
        float w = in - a1 * z1 - a2 * z2;
        float out = b0 * w + b1 * z1 + b2 * z2;
        z2 = z1; z1 = w; return out;
    }
    void reset() { z1 = z2 = 0.f; }
};

static const int SHINJUKU_TRACKS = 8;
static const int SHINJUKU_EQ_BANDS = 12;
static const float SHINJUKU_EQ_FREQS[SHINJUKU_EQ_BANDS] = {
    31.f, 63.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 3150.f, 5000.f, 8000.f, 12500.f, 16000.f
};

struct SHINJUKU : Module {
    enum ParamId {
        ENUMS(LEVEL_PARAM, SHINJUKU_TRACKS),
        ENUMS(DUCK_PARAM, SHINJUKU_TRACKS),
        ENUMS(MUTE_PARAM, SHINJUKU_TRACKS),
        ENUMS(SOLO_PARAM, SHINJUKU_TRACKS),
        ENUMS(EQ_PARAM, SHINJUKU_EQ_BANDS),
        PARAMS_LEN
    };
    enum InputId {
        ENUMS(LEFT_INPUT, SHINJUKU_TRACKS),
        ENUMS(RIGHT_INPUT, SHINJUKU_TRACKS),
        ENUMS(LEVEL_CV_INPUT, SHINJUKU_TRACKS),
        ENUMS(DUCK_INPUT, SHINJUKU_TRACKS),
        ENUMS(MUTE_TRIG_INPUT, SHINJUKU_TRACKS),
        ENUMS(SOLO_TRIG_INPUT, SHINJUKU_TRACKS),
        CHAIN_LEFT_INPUT, CHAIN_RIGHT_INPUT,
        INPUTS_LEN
    };
    enum OutputId { LEFT_OUTPUT, RIGHT_OUTPUT, OUTPUTS_LEN };
    enum LightId {
        ENUMS(MUTE_LIGHT, SHINJUKU_TRACKS),
        ENUMS(SOLO_LIGHT, SHINJUKU_TRACKS),
        LIGHTS_LEN
    };

    static constexpr int DELAY_BUFFER_SIZE = 2048;
    float delayBuffer[SHINJUKU_TRACKS][DELAY_BUFFER_SIZE] = {};
    int delayWriteIndex[SHINJUKU_TRACKS] = {};

    bool muteState[SHINJUKU_TRACKS] = {};
    bool soloState[SHINJUKU_TRACKS] = {};
    dsp::SchmittTrigger muteTrigger[SHINJUKU_TRACKS];
    dsp::SchmittTrigger soloTrigger[SHINJUKU_TRACKS];

    ShinjukuBiquadPeakEQ eqFiltersL[SHINJUKU_EQ_BANDS];
    ShinjukuBiquadPeakEQ eqFiltersR[SHINJUKU_EQ_BANDS];
    float lastEqGains[SHINJUKU_EQ_BANDS] = {};
    float lastSampleRate = 0.f;

    SHINJUKU() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int t = 0; t < SHINJUKU_TRACKS; t++) {
            configParam(LEVEL_PARAM + t, 0.0f, 2.0f, 1.0f, "Level");
            configParam(DUCK_PARAM + t, 0.0f, 1.0f, 0.0f, "Duck");
            configSwitch(MUTE_PARAM + t, 0.0f, 1.0f, 0.0f, "Mute", {"Unmuted", "Muted"});
            configSwitch(SOLO_PARAM + t, 0.0f, 1.0f, 0.0f, "Solo", {"Off", "Solo"});
            configInput(LEFT_INPUT + t, "Left"); configInput(RIGHT_INPUT + t, "Right");
            configInput(LEVEL_CV_INPUT + t, "Level CV"); configInput(DUCK_INPUT + t, "Duck");
            configInput(MUTE_TRIG_INPUT + t, "Mute Trigger"); configInput(SOLO_TRIG_INPUT + t, "Solo Trigger");
        }
        configInput(CHAIN_LEFT_INPUT, "Chain Left"); configInput(CHAIN_RIGHT_INPUT, "Chain Right");
        configOutput(LEFT_OUTPUT, "Mix Left"); configOutput(RIGHT_OUTPUT, "Mix Right");
        for (int b = 0; b < SHINJUKU_EQ_BANDS; b++)
            configParam(EQ_PARAM + b, -12.f, 12.f, 0.f, "EQ", " dB");
    }

    void process(const ProcessArgs& args) override {
        bool hasSolo = false;
        for (int t = 0; t < SHINJUKU_TRACKS; t++)
            if (params[SOLO_PARAM + t].getValue() > 0.5f) { hasSolo = true; break; }

        float mixL = 0.0f, mixR = 0.0f;
        for (int t = 0; t < SHINJUKU_TRACKS; t++) {
            if (inputs[MUTE_TRIG_INPUT + t].isConnected())
                if (muteTrigger[t].process(inputs[MUTE_TRIG_INPUT + t].getVoltage())) {
                    muteState[t] = !muteState[t];
                    params[MUTE_PARAM + t].setValue(muteState[t] ? 1.0f : 0.0f);
                }
            if (inputs[SOLO_TRIG_INPUT + t].isConnected())
                if (soloTrigger[t].process(inputs[SOLO_TRIG_INPUT + t].getVoltage())) {
                    soloState[t] = !soloState[t];
                    params[SOLO_PARAM + t].setValue(soloState[t] ? 1.0f : 0.0f);
                }

            bool muted = params[MUTE_PARAM + t].getValue() > 0.5f;
            bool soloed = params[SOLO_PARAM + t].getValue() > 0.5f;
            bool soloMuted = hasSolo && !soloed;

            lights[MUTE_LIGHT + t].setBrightness((muted || soloMuted) ? 1.0f : 0.0f);
            lights[SOLO_LIGHT + t].setBrightness(soloed ? 1.0f : 0.0f);

            if (soloMuted || muted) continue;

            float leftIn = inputs[LEFT_INPUT + t].getVoltage();
            float rightIn;
            if (inputs[RIGHT_INPUT + t].isConnected()) {
                rightIn = inputs[RIGHT_INPUT + t].getVoltage();
            } else if (inputs[LEFT_INPUT + t].isConnected()) {
                int delaySamples = clamp((int)(0.02f * args.sampleRate), 1, DELAY_BUFFER_SIZE - 1);
                int readIndex = (delayWriteIndex[t] - delaySamples + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
                rightIn = delayBuffer[t][readIndex];
                delayBuffer[t][delayWriteIndex[t]] = leftIn;
                delayWriteIndex[t] = (delayWriteIndex[t] + 1) % DELAY_BUFFER_SIZE;
            } else { rightIn = 0.0f; }

            float level = params[LEVEL_PARAM + t].getValue();
            if (inputs[LEVEL_CV_INPUT + t].isConnected()) {
                float cv = clamp(inputs[LEVEL_CV_INPUT + t].getVoltage() / 10.0f, -1.0f, 1.0f);
                level = clamp(level + cv, 0.0f, 2.0f);
            }

            float duck = 1.0f;
            if (inputs[DUCK_INPUT + t].isConnected()) {
                float duckCV = clamp(inputs[DUCK_INPUT + t].getVoltage() / 10.0f, 0.0f, 1.0f);
                duck = clamp(1.0f - (duckCV * params[DUCK_PARAM + t].getValue() * 3.0f), 0.0f, 1.0f);
            }

            mixL += leftIn * level * duck;
            mixR += rightIn * level * duck;
        }

        mixL += inputs[CHAIN_LEFT_INPUT].getVoltage();
        mixR += inputs[CHAIN_RIGHT_INPUT].getVoltage();

        for (int b = 0; b < SHINJUKU_EQ_BANDS; b++) {
            mixL = eqFiltersL[b].process(mixL);
            mixR = eqFiltersR[b].process(mixR);
        }

        outputs[LEFT_OUTPUT].setVoltage(clamp(mixL, -10.f, 10.f));
        outputs[RIGHT_OUTPUT].setVoltage(clamp(mixR, -10.f, 10.f));

        bool needsUpdate = (args.sampleRate != lastSampleRate);
        for (int b = 0; b < SHINJUKU_EQ_BANDS; b++) {
            float gain = params[EQ_PARAM + b].getValue();
            if (gain != lastEqGains[b]) { needsUpdate = true; lastEqGains[b] = gain; }
        }
        if (needsUpdate) {
            lastSampleRate = args.sampleRate;
            for (int b = 0; b < SHINJUKU_EQ_BANDS; b++) {
                eqFiltersL[b].setParams(args.sampleRate, SHINJUKU_EQ_FREQS[b], lastEqGains[b]);
                eqFiltersR[b].setParams(args.sampleRate, SHINJUKU_EQ_FREQS[b], lastEqGains[b]);
            }
        }
    }
};

struct SHINJUKUWidget : ModuleWidget {
    SHINJUKUWidget(SHINJUKU* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "SHINJUKU.png")));
        box.size = Vec(32 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        float trackWidth = 4 * RACK_GRID_WIDTH;
        for (int t = 0; t < SHINJUKU_TRACKS; t++) {
            float trackX = t * trackWidth;
            float centerX = trackX + trackWidth / 2;

            addInput(createInputCentered<PJ301MPort>(Vec(centerX - 15, 59), module, SHINJUKU::LEFT_INPUT + t));
            addInput(createInputCentered<PJ301MPort>(Vec(centerX + 15, 59), module, SHINJUKU::RIGHT_INPUT + t));

            addParam(createParamCentered<RoundBlackKnob>(Vec(centerX, 123), module, SHINJUKU::LEVEL_PARAM + t));
            addInput(createInputCentered<PJ301MPort>(Vec(centerX, 161), module, SHINJUKU::LEVEL_CV_INPUT + t));

            addParam(createParamCentered<RoundBlackKnob>(Vec(centerX, 216), module, SHINJUKU::DUCK_PARAM + t));
            addInput(createInputCentered<PJ301MPort>(Vec(centerX, 254), module, SHINJUKU::DUCK_INPUT + t));

            addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(Vec(centerX - 15, 292), module, SHINJUKU::MUTE_PARAM + t, SHINJUKU::MUTE_LIGHT + t));
            addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(Vec(centerX + 15, 292), module, SHINJUKU::SOLO_PARAM + t, SHINJUKU::SOLO_LIGHT + t));
            addInput(createInputCentered<PJ301MPort>(Vec(centerX - 15, 316), module, SHINJUKU::MUTE_TRIG_INPUT + t));
            addInput(createInputCentered<PJ301MPort>(Vec(centerX + 15, 316), module, SHINJUKU::SOLO_TRIG_INPUT + t));
        }

        addInput(createInputCentered<PJ301MPort>(Vec(15, 343), module, SHINJUKU::CHAIN_LEFT_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(15, 368), module, SHINJUKU::CHAIN_RIGHT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(box.size.x - 15, 343), module, SHINJUKU::LEFT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(box.size.x - 15, 368), module, SHINJUKU::RIGHT_OUTPUT));

        float eqStartX = 38.f, eqEndX = box.size.x - 38.f;
        float eqSpacing = (eqEndX - eqStartX) / (SHINJUKU_EQ_BANDS - 1);
        for (int b = 0; b < SHINJUKU_EQ_BANDS; b++) {
            float x = eqStartX + b * eqSpacing;
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(x, 355), module, SHINJUKU::EQ_PARAM + b));
        }
    }
};

Model* modelSHINJUKU = createModel<SHINJUKU, SHINJUKUWidget>("SHINJUKU");
