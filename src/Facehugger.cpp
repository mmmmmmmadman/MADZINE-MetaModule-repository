#include "plugin.hpp"

// ChaosGenerator - Lorenz Attractor (same as EllenRipley)
struct FacehuggerChaosGenerator {
    float x = 0.1f, y = 0.1f, z = 0.1f;

    void reset() { x = 0.1f; y = 0.1f; z = 0.1f; }

    float process(float rate) {
        float dt = rate * 0.001f;
        float dx = 7.5f * (y - x);
        float dy = x * (30.9f - z) - y;
        float dz = x * y - 1.02f * z;
        x += dx * dt; y += dy * dt; z += dz * dt;
        if (std::isnan(x) || std::isnan(y) || std::isnan(z) ||
            std::abs(x) > 100.0f || std::abs(y) > 100.0f || std::abs(z) > 100.0f) {
            reset();
        }
        return clamp(x * 0.1f, -1.0f, 1.0f);
    }
};

// GrainProcessor - 16 Grains (same as EllenRipley)
struct FacehuggerGrainProcessor {
    static constexpr int GRAIN_BUFFER_SIZE = 8192;
    float grainBuffer[GRAIN_BUFFER_SIZE] = {};
    int grainWriteIndex = 0;

    struct Grain {
        bool active = false;
        float position = 0.0f;
        float size = 0.0f;
        float envelope = 0.0f;
        float direction = 1.0f;
        float pitch = 1.0f;
    };

    static constexpr int MAX_GRAINS = 16;
    Grain grains[MAX_GRAINS];
    float phase = 0.0f;

    void reset() {
        for (int i = 0; i < GRAIN_BUFFER_SIZE; i++) grainBuffer[i] = 0.0f;
        grainWriteIndex = 0;
        for (int i = 0; i < MAX_GRAINS; i++) grains[i].active = false;
        phase = 0.0f;
    }

    float process(float input, float grainSize, float density, float position,
                  bool chaosEnabled, float chaosOutput, float sampleRate) {
        grainBuffer[grainWriteIndex] = input;
        grainWriteIndex = (grainWriteIndex + 1) % GRAIN_BUFFER_SIZE;

        float grainSizeMs = grainSize * 99.0f + 1.0f;
        float grainSamples = (grainSizeMs / 1000.0f) * sampleRate;

        float densityValue = density;
        if (chaosEnabled) densityValue += chaosOutput * 0.3f;
        densityValue = clamp(densityValue, 0.0f, 1.0f);

        float triggerRate = densityValue * 50.0f + 1.0f;
        phase += triggerRate / sampleRate;

        if (phase >= 1.0f) {
            phase -= 1.0f;
            for (int i = 0; i < MAX_GRAINS; i++) {
                if (!grains[i].active) {
                    grains[i].active = true;
                    grains[i].size = grainSamples;
                    grains[i].envelope = 0.0f;
                    float pos = position;
                    if (chaosEnabled) {
                        pos += chaosOutput * 20.0f;
                        grains[i].direction = (random::uniform() < 0.3f) ? -1.0f : 1.0f;
                        grains[i].pitch = (densityValue > 0.7f && random::uniform() < 0.2f)
                            ? (random::uniform() < 0.5f ? 0.5f : 2.0f) : 1.0f;
                    } else {
                        grains[i].direction = 1.0f;
                        grains[i].pitch = 1.0f;
                    }
                    pos = clamp(pos, 0.0f, 1.0f);
                    grains[i].position = pos * GRAIN_BUFFER_SIZE;
                    break;
                }
            }
        }

        float output = 0.0f;
        int activeGrains = 0;
        for (int i = 0; i < MAX_GRAINS; i++) {
            if (grains[i].active) {
                float envPhase = grains[i].envelope / grains[i].size;
                if (envPhase >= 1.0f) { grains[i].active = false; continue; }
                float env = 0.5f * (1.0f - cos(envPhase * 2.0f * M_PI));
                int readPos = ((int)grains[i].position % GRAIN_BUFFER_SIZE + GRAIN_BUFFER_SIZE) % GRAIN_BUFFER_SIZE;
                output += grainBuffer[readPos] * env;
                grains[i].position += grains[i].direction * grains[i].pitch;
                while (grains[i].position >= GRAIN_BUFFER_SIZE) grains[i].position -= GRAIN_BUFFER_SIZE;
                while (grains[i].position < 0) grains[i].position += GRAIN_BUFFER_SIZE;
                grains[i].envelope += 1.0f;
                activeGrains++;
            }
        }
        if (activeGrains > 0) output /= sqrt((float)activeGrains);
        return output;
    }
};

struct Facehugger : Module {
    enum ParamIds {
        SIZE_PARAM, BREAK_PARAM, SHIFT_PARAM,
        MIX_PARAM, CHAOS_PARAM, RATE_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        LEFT_INPUT, RIGHT_INPUT,
        SIZE_CV_INPUT, BREAK_CV_INPUT, SHIFT_CV_INPUT,
        MIX_CV_INPUT, CHAOS_CV_INPUT, RATE_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        LEFT_OUTPUT, RIGHT_OUTPUT, CHAOS_OUTPUT, SH_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds { NUM_LIGHTS };

    FacehuggerChaosGenerator chaosGen;
    FacehuggerGrainProcessor leftGrainProcessor;
    FacehuggerGrainProcessor rightGrainProcessor;
    float lastSHValue = 0.0f;
    float shPhase = 0.0f;

    Facehugger() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(SIZE_PARAM, 0.0f, 1.0f, 0.3f, "Size");
        configParam(BREAK_PARAM, 0.0f, 1.0f, 0.4f, "Break");
        configParam(SHIFT_PARAM, 0.0f, 1.0f, 0.5f, "Shift");
        configParam(MIX_PARAM, 0.0f, 1.0f, 0.5f, "Mix", "%", 0.f, 100.f);
        configParam(CHAOS_PARAM, 0.0f, 1.0f, 0.0f, "Chaos", "%", 0.f, 100.f);
        configParam(RATE_PARAM, 0.01f, 2.0f, 0.5f, "Rate", "x");
        configInput(LEFT_INPUT, "Left Audio");
        configInput(RIGHT_INPUT, "Right Audio");
        configInput(SIZE_CV_INPUT, "Size CV");
        configInput(BREAK_CV_INPUT, "Break CV");
        configInput(SHIFT_CV_INPUT, "Shift CV");
        configInput(MIX_CV_INPUT, "Mix CV");
        configInput(CHAOS_CV_INPUT, "Chaos CV");
        configInput(RATE_CV_INPUT, "Rate CV");
        configOutput(LEFT_OUTPUT, "Left Audio");
        configOutput(RIGHT_OUTPUT, "Right Audio");
        configOutput(CHAOS_OUTPUT, "Chaos CV");
        configOutput(SH_OUTPUT, "Sample & Hold CV");
    }

    void onReset() override {
        chaosGen.reset();
        leftGrainProcessor.reset();
        rightGrainProcessor.reset();
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate <= 0) return;

        // Chaos
        float chaosAmount = params[CHAOS_PARAM].getValue();
        if (inputs[CHAOS_CV_INPUT].isConnected())
            chaosAmount += inputs[CHAOS_CV_INPUT].getVoltage() * 0.1f;
        chaosAmount = clamp(chaosAmount, 0.0f, 1.0f);

        float chaosRate = params[RATE_PARAM].getValue();
        if (inputs[RATE_CV_INPUT].isConnected())
            chaosRate += inputs[RATE_CV_INPUT].getVoltage() * 0.2f;
        chaosRate = clamp(chaosRate, 0.01f, 2.0f);

        bool chaosEnabled = chaosAmount > 0.0f;
        float chaosRaw = 0.0f, chaosSH = 0.0f;
        if (chaosEnabled) {
            chaosRaw = chaosGen.process(chaosRate) * chaosAmount;
            float shRate = chaosRate * 10.0f;
            shPhase += shRate / args.sampleRate;
            if (shPhase >= 1.0f) { lastSHValue = chaosRaw; shPhase = 0.0f; }
            chaosSH = lastSHValue;
        }
        outputs[CHAOS_OUTPUT].setVoltage(chaosRaw * 5.0f);
        outputs[SH_OUTPUT].setVoltage(chaosSH * 5.0f);

        // Input
        float leftInput = inputs[LEFT_INPUT].getVoltage();
        float rightInput = inputs[RIGHT_INPUT].isConnected() ? inputs[RIGHT_INPUT].getVoltage() : leftInput;
        if (!std::isfinite(leftInput)) leftInput = 0.0f;
        if (!std::isfinite(rightInput)) rightInput = 0.0f;

        // Size
        float grainSize = params[SIZE_PARAM].getValue();
        if (inputs[SIZE_CV_INPUT].isConnected()) grainSize += inputs[SIZE_CV_INPUT].getVoltage() * 0.1f;
        grainSize = clamp(grainSize, 0.0f, 1.0f);

        // Break
        float grainDensity = params[BREAK_PARAM].getValue();
        if (inputs[BREAK_CV_INPUT].isConnected()) grainDensity += inputs[BREAK_CV_INPUT].getVoltage() * 0.1f;
        grainDensity = clamp(grainDensity, 0.0f, 1.0f);

        // Shift
        float grainPosition = params[SHIFT_PARAM].getValue();
        if (inputs[SHIFT_CV_INPUT].isConnected()) grainPosition += inputs[SHIFT_CV_INPUT].getVoltage() * 0.1f;
        grainPosition = clamp(grainPosition, 0.0f, 1.0f);

        // Grain processing
        float leftGrain = leftGrainProcessor.process(leftInput, grainSize, grainDensity, grainPosition, chaosEnabled, chaosRaw, args.sampleRate);
        float rightGrain = rightGrainProcessor.process(rightInput, grainSize, grainDensity, grainPosition, chaosEnabled, chaosRaw, args.sampleRate);

        // Mix
        float mix = params[MIX_PARAM].getValue();
        if (inputs[MIX_CV_INPUT].isConnected()) mix += inputs[MIX_CV_INPUT].getVoltage() * 0.1f;
        mix = clamp(mix, 0.0f, 1.0f);

        float leftOut = leftInput * (1.0f - mix) + leftGrain * mix;
        float rightOut = rightInput * (1.0f - mix) + rightGrain * mix;
        if (!std::isfinite(leftOut)) leftOut = 0.0f;
        if (!std::isfinite(rightOut)) rightOut = 0.0f;

        outputs[LEFT_OUTPUT].setVoltage(leftOut);
        outputs[RIGHT_OUTPUT].setVoltage(rightOut);
    }
};

struct FacehuggerWidget : ModuleWidget {
    FacehuggerWidget(Facehugger* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "Facehugger.png")));
        box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        float leftX = 15.0f, rightX = 45.0f;

        // Knobs: SIZE/BREAK, SHIFT/MIX, CHAOS/RATE
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 72), module, Facehugger::SIZE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 72), module, Facehugger::BREAK_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 117), module, Facehugger::SHIFT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 117), module, Facehugger::MIX_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 162), module, Facehugger::CHAOS_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 162), module, Facehugger::RATE_PARAM));

        // CV inputs
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 197), module, Facehugger::SIZE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 197), module, Facehugger::BREAK_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 232), module, Facehugger::SHIFT_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 232), module, Facehugger::MIX_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 267), module, Facehugger::CHAOS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 267), module, Facehugger::RATE_CV_INPUT));

        // Chaos / S&H outputs
        addOutput(createOutputCentered<PJ301MPort>(Vec(leftX, 302), module, Facehugger::CHAOS_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 302), module, Facehugger::SH_OUTPUT));

        // Audio I/O
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 343), module, Facehugger::LEFT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 343), module, Facehugger::LEFT_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 368), module, Facehugger::RIGHT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 368), module, Facehugger::RIGHT_OUTPUT));
    }
};

Model* modelFacehugger = createModel<Facehugger, FacehuggerWidget>("Facehugger");
