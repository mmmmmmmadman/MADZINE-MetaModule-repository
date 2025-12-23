#include "plugin.hpp"

struct U8 : Module {
    enum ParamId {
        LEVEL_PARAM,
        DUCK_LEVEL_PARAM,
        MUTE_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        LEFT_INPUT,
        RIGHT_INPUT,
        DUCK_INPUT,
        LEVEL_CV_INPUT,
        MUTE_TRIG_INPUT,
        CHAIN_LEFT_INPUT,
        CHAIN_RIGHT_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        MUTE_LIGHT,
        LIGHTS_LEN
    };

    static constexpr int DELAY_BUFFER_SIZE = 2048;
    static constexpr int MAX_POLY = 16;
    float delayBuffer[MAX_POLY][DELAY_BUFFER_SIZE];
    int delayWriteIndex[MAX_POLY];

    bool muteState = false;
    dsp::SchmittTrigger muteTrigger;

    U8() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(LEVEL_PARAM, 0.0f, 2.0f, 1.0f, "Level");
        configParam(DUCK_LEVEL_PARAM, 0.0f, 1.0f, 0.0f, "Duck Level");
        configSwitch(MUTE_PARAM, 0.0f, 1.0f, 0.0f, "Mute", {"Unmuted", "Muted"});

        configInput(LEFT_INPUT, "Left Audio");
        configInput(RIGHT_INPUT, "Right Audio");
        configInput(DUCK_INPUT, "Duck Signal");
        configInput(LEVEL_CV_INPUT, "Level CV");
        configInput(MUTE_TRIG_INPUT, "Mute Trigger");
        configInput(CHAIN_LEFT_INPUT, "Chain Left");
        configInput(CHAIN_RIGHT_INPUT, "Chain Right");

        configOutput(LEFT_OUTPUT, "Left Audio");
        configOutput(RIGHT_OUTPUT, "Right Audio");

        configLight(MUTE_LIGHT, "Mute Indicator");

        for (int c = 0; c < MAX_POLY; c++) {
            for (int i = 0; i < DELAY_BUFFER_SIZE; i++) {
                delayBuffer[c][i] = 0.0f;
            }
            delayWriteIndex[c] = 0;
        }
    }

    void process(const ProcessArgs& args) override {
        // Handle mute trigger (monophonic)
        if (inputs[MUTE_TRIG_INPUT].isConnected()) {
            if (muteTrigger.process(inputs[MUTE_TRIG_INPUT].getVoltage())) {
                muteState = !muteState;
                params[MUTE_PARAM].setValue(muteState ? 1.0f : 0.0f);
            }
        }

        bool muted = params[MUTE_PARAM].getValue() > 0.5f;
        lights[MUTE_LIGHT].setBrightness(muted ? 1.0f : 0.0f);

        // Get polyphonic channel counts
        int leftChannels = inputs[LEFT_INPUT].getChannels();
        int rightChannels = inputs[RIGHT_INPUT].getChannels();
        int chainLeftChannels = inputs[CHAIN_LEFT_INPUT].getChannels();
        int chainRightChannels = inputs[CHAIN_RIGHT_INPUT].getChannels();

        // Determine output channel count
        int outputLeftChannels = std::max({leftChannels, chainLeftChannels, 1});
        int outputRightChannels = std::max({rightChannels, chainRightChannels, 1});

        // If left is connected but right isn't, use delay for stereo effect
        bool useDelay = inputs[LEFT_INPUT].isConnected() && !inputs[RIGHT_INPUT].isConnected();
        if (useDelay) {
            outputRightChannels = outputLeftChannels;
        }

        outputs[LEFT_OUTPUT].setChannels(outputLeftChannels);
        outputs[RIGHT_OUTPUT].setChannels(outputRightChannels);

        // Get duck and level parameters (can be polyphonic)
        int duckChannels = inputs[DUCK_INPUT].getChannels();
        int levelCvChannels = inputs[LEVEL_CV_INPUT].getChannels();

        float levelParam = params[LEVEL_PARAM].getValue();
        float duckAmount = params[DUCK_LEVEL_PARAM].getValue();

        // Process left output channels
        for (int c = 0; c < outputLeftChannels; c++) {
            float leftInput = (c < leftChannels) ? inputs[LEFT_INPUT].getPolyVoltage(c) : 0.0f;
            float chainLeftInput = (c < chainLeftChannels) ? inputs[CHAIN_LEFT_INPUT].getPolyVoltage(c) : 0.0f;

            // Apply ducking (use matching channel or channel 0)
            float duckCV = 0.0f;
            if (inputs[DUCK_INPUT].isConnected()) {
                int duckChan = (c < duckChannels) ? c : 0;
                duckCV = clamp(inputs[DUCK_INPUT].getPolyVoltage(duckChan) / 10.0f, 0.0f, 1.0f);
            }
            float sidechainCV = clamp(1.0f - (duckCV * duckAmount * 3.0f), 0.0f, 1.0f);

            // Apply level control
            float level = levelParam;
            if (inputs[LEVEL_CV_INPUT].isConnected()) {
                int levelChan = (c < levelCvChannels) ? c : 0;
                float cvLevel = clamp(inputs[LEVEL_CV_INPUT].getPolyVoltage(levelChan) / 10.0f, 0.0f, 1.0f);
                level = levelParam * cvLevel;
            }

            leftInput *= level * sidechainCV;

            if (muted) {
                leftInput = 0.0f;
            }

            outputs[LEFT_OUTPUT].setVoltage(leftInput + chainLeftInput, c);
        }

        // Process right output channels
        for (int c = 0; c < outputRightChannels; c++) {
            float rightInput = 0.0f;

            if (useDelay && c < leftChannels) {
                // Use delay for right channel when only left is connected
                int delaySamples = (int)(0.02f * args.sampleRate);
                delaySamples = clamp(delaySamples, 1, DELAY_BUFFER_SIZE - 1);

                int readIndex = (delayWriteIndex[c] - delaySamples + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
                rightInput = delayBuffer[c][readIndex];

                // Store left input in delay buffer
                delayBuffer[c][delayWriteIndex[c]] = inputs[LEFT_INPUT].getPolyVoltage(c);
                delayWriteIndex[c] = (delayWriteIndex[c] + 1) % DELAY_BUFFER_SIZE;
            } else if (c < rightChannels) {
                rightInput = inputs[RIGHT_INPUT].getPolyVoltage(c);
            }

            float chainRightInput = (c < chainRightChannels) ? inputs[CHAIN_RIGHT_INPUT].getPolyVoltage(c) : 0.0f;

            // Apply ducking
            float duckCV = 0.0f;
            if (inputs[DUCK_INPUT].isConnected()) {
                int duckChan = (c < duckChannels) ? c : 0;
                duckCV = clamp(inputs[DUCK_INPUT].getPolyVoltage(duckChan) / 10.0f, 0.0f, 1.0f);
            }
            float sidechainCV = clamp(1.0f - (duckCV * duckAmount * 3.0f), 0.0f, 1.0f);

            // Apply level control
            float level = levelParam;
            if (inputs[LEVEL_CV_INPUT].isConnected()) {
                int levelChan = (c < levelCvChannels) ? c : 0;
                float cvLevel = clamp(inputs[LEVEL_CV_INPUT].getPolyVoltage(levelChan) / 10.0f, 0.0f, 1.0f);
                level = levelParam * cvLevel;
            }

            rightInput *= level * sidechainCV;

            if (muted) {
                rightInput = 0.0f;
            }

            outputs[RIGHT_OUTPUT].setVoltage(rightInput + chainRightInput, c);
        }
    }

    void processBypass(const ProcessArgs& args) override {
        int chainLeftChannels = inputs[CHAIN_LEFT_INPUT].getChannels();
        int chainRightChannels = inputs[CHAIN_RIGHT_INPUT].getChannels();

        outputs[LEFT_OUTPUT].setChannels(chainLeftChannels);
        outputs[RIGHT_OUTPUT].setChannels(chainRightChannels);

        for (int c = 0; c < chainLeftChannels; c++) {
            outputs[LEFT_OUTPUT].setVoltage(inputs[CHAIN_LEFT_INPUT].getPolyVoltage(c), c);
        }

        for (int c = 0; c < chainRightChannels; c++) {
            outputs[RIGHT_OUTPUT].setVoltage(inputs[CHAIN_RIGHT_INPUT].getPolyVoltage(c), c);
        }
    }
};

struct U8Widget : ModuleWidget {
    U8Widget(U8* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "U8.png")));
        
        box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        float centerX = box.size.x / 2;

        addInput(createInputCentered<PJ301MPort>(Vec(15, 59), module, U8::LEFT_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(centerX + 15, 59), module, U8::RIGHT_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(Vec(centerX, 123), module, U8::LEVEL_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(centerX, 161), module, U8::LEVEL_CV_INPUT));
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(centerX, 216), module, U8::DUCK_LEVEL_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(centerX, 254), module, U8::DUCK_INPUT));
        
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(Vec(centerX, 292), module, U8::MUTE_PARAM, U8::MUTE_LIGHT));
        addInput(createInputCentered<PJ301MPort>(Vec(centerX, 316), module, U8::MUTE_TRIG_INPUT));
        
        addInput(createInputCentered<PJ301MPort>(Vec(15, 343), module, U8::CHAIN_LEFT_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(15, 368), module, U8::CHAIN_RIGHT_INPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(centerX + 15, 343), module, U8::LEFT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(centerX + 15, 368), module, U8::RIGHT_OUTPUT));
    }
};

Model* modelU8 = createModel<U8, U8Widget>("U8");