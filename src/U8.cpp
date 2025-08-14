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
    float delayBuffer[DELAY_BUFFER_SIZE];
    int delayWriteIndex = 0;

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
        
        for (int i = 0; i < DELAY_BUFFER_SIZE; i++) {
            delayBuffer[i] = 0.0f;
        }
    }

    void process(const ProcessArgs& args) override {
        if (inputs[MUTE_TRIG_INPUT].isConnected()) {
            if (muteTrigger.process(inputs[MUTE_TRIG_INPUT].getVoltage())) {
                muteState = !muteState;
                params[MUTE_PARAM].setValue(muteState ? 1.0f : 0.0f);
            }
        }
        
        float leftInput = inputs[LEFT_INPUT].getVoltage();
        float rightInput;
        
        bool leftConnected = inputs[LEFT_INPUT].isConnected();
        bool rightConnected = inputs[RIGHT_INPUT].isConnected();
        
        if (leftConnected && !rightConnected) {
            int delaySamples = (int)(0.02f * args.sampleRate);
            delaySamples = clamp(delaySamples, 1, DELAY_BUFFER_SIZE - 1);
            
            int readIndex = (delayWriteIndex - delaySamples + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
            rightInput = delayBuffer[readIndex];
            
            delayBuffer[delayWriteIndex] = leftInput;
            delayWriteIndex = (delayWriteIndex + 1) % DELAY_BUFFER_SIZE;
        } else if (rightConnected) {
            rightInput = inputs[RIGHT_INPUT].getVoltage();
        } else {
            rightInput = leftInput;
        }
        
        float duckCV = clamp(inputs[DUCK_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
        float duckAmount = params[DUCK_LEVEL_PARAM].getValue();
        float sidechainCV = clamp(1.0f - (duckCV * duckAmount * 3.0f), 0.0f, 1.0f);
        
        float levelParam = params[LEVEL_PARAM].getValue();
        if (inputs[LEVEL_CV_INPUT].isConnected()) {
            float cvInput = clamp(inputs[LEVEL_CV_INPUT].getVoltage() / 5.0f, 0.0f, 2.0f);
            levelParam *= cvInput;
        }
        
        leftInput *= levelParam * sidechainCV;
        rightInput *= levelParam * sidechainCV;
        
        bool muted = params[MUTE_PARAM].getValue() > 0.5f;
        lights[MUTE_LIGHT].setBrightness(muted ? 1.0f : 0.0f);
        
        if (muted) {
            leftInput = 0.0f;
            rightInput = 0.0f;
        }
        
        float chainLeftInput = inputs[CHAIN_LEFT_INPUT].getVoltage();
        float chainRightInput = inputs[CHAIN_RIGHT_INPUT].getVoltage();
        
        outputs[LEFT_OUTPUT].setVoltage(leftInput + chainLeftInput);
        outputs[RIGHT_OUTPUT].setVoltage(rightInput + chainRightInput);
    }
    
    void processBypass(const ProcessArgs& args) override {
        float chainLeftInput = inputs[CHAIN_LEFT_INPUT].getVoltage();
        float chainRightInput = inputs[CHAIN_RIGHT_INPUT].getVoltage();
        
        outputs[LEFT_OUTPUT].setVoltage(chainLeftInput);
        outputs[RIGHT_OUTPUT].setVoltage(chainRightInput);
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