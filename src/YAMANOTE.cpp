#include "plugin.hpp"

struct YAMANOTE : Module {
    enum ParamId {
        CH1_SEND_A_PARAM,
        CH1_SEND_B_PARAM,
        CH2_SEND_A_PARAM,
        CH2_SEND_B_PARAM,
        CH3_SEND_A_PARAM,
        CH3_SEND_B_PARAM,
        CH4_SEND_A_PARAM,
        CH4_SEND_B_PARAM,
        CH5_SEND_A_PARAM,
        CH5_SEND_B_PARAM,
        CH6_SEND_A_PARAM,
        CH6_SEND_B_PARAM,
        CH7_SEND_A_PARAM,
        CH7_SEND_B_PARAM,
        CH8_SEND_A_PARAM,
        CH8_SEND_B_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CH1_L_INPUT,
        CH1_R_INPUT,
        CH2_L_INPUT,
        CH2_R_INPUT,
        CH3_L_INPUT,
        CH3_R_INPUT,
        CH4_L_INPUT,
        CH4_R_INPUT,
        CH5_L_INPUT,
        CH5_R_INPUT,
        CH6_L_INPUT,
        CH6_R_INPUT,
        CH7_L_INPUT,
        CH7_R_INPUT,
        CH8_L_INPUT,
        CH8_R_INPUT,
        CHAIN_L_INPUT,
        CHAIN_R_INPUT,
        RETURN_A_L_INPUT,
        RETURN_A_R_INPUT,
        RETURN_B_L_INPUT,
        RETURN_B_R_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        SEND_A_L_OUTPUT,
        SEND_A_R_OUTPUT,
        SEND_B_L_OUTPUT,
        SEND_B_R_OUTPUT,
        MIX_L_OUTPUT,
        MIX_R_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    static constexpr int MAX_POLY = 16;

    YAMANOTE() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        
        for (int i = 0; i < 8; ++i) {
            configParam(CH1_SEND_A_PARAM + i * 2, 0.0f, 1.0f, 0.0f, string::f("CH%d Send A", i + 1));
            configParam(CH1_SEND_B_PARAM + i * 2, 0.0f, 1.0f, 0.0f, string::f("CH%d Send B", i + 1));
            
            configInput(CH1_L_INPUT + i * 2, string::f("CH%d Left", i + 1));
            configInput(CH1_R_INPUT + i * 2, string::f("CH%d Right", i + 1));
        }
        
        configInput(CHAIN_L_INPUT, "Chain Left");
        configInput(CHAIN_R_INPUT, "Chain Right");
        configInput(RETURN_A_L_INPUT, "Return A Left");
        configInput(RETURN_A_R_INPUT, "Return A Right");
        configInput(RETURN_B_L_INPUT, "Return B Left");
        configInput(RETURN_B_R_INPUT, "Return B Right");
        
        configOutput(SEND_A_L_OUTPUT, "Send A Left");
        configOutput(SEND_A_R_OUTPUT, "Send A Right");
        configOutput(SEND_B_L_OUTPUT, "Send B Left");
        configOutput(SEND_B_R_OUTPUT, "Send B Right");
        configOutput(MIX_L_OUTPUT, "Mix Left");
        configOutput(MIX_R_OUTPUT, "Mix Right");
    }

    void process(const ProcessArgs& args) override {
        // Determine maximum polyphonic channels across all inputs
        int maxChannels = 1;

        // Check channel inputs
        for (int i = 0; i < 8; ++i) {
            int leftChannels = inputs[CH1_L_INPUT + i * 2].getChannels();
            int rightChannels = inputs[CH1_R_INPUT + i * 2].getChannels();
            maxChannels = std::max({maxChannels, leftChannels, rightChannels});
        }

        // Check return and chain inputs
        maxChannels = std::max({maxChannels,
            inputs[CHAIN_L_INPUT].getChannels(),
            inputs[CHAIN_R_INPUT].getChannels(),
            inputs[RETURN_A_L_INPUT].getChannels(),
            inputs[RETURN_A_R_INPUT].getChannels(),
            inputs[RETURN_B_L_INPUT].getChannels(),
            inputs[RETURN_B_R_INPUT].getChannels()
        });

        // Set output channels
        outputs[SEND_A_L_OUTPUT].setChannels(maxChannels);
        outputs[SEND_A_R_OUTPUT].setChannels(maxChannels);
        outputs[SEND_B_L_OUTPUT].setChannels(maxChannels);
        outputs[SEND_B_R_OUTPUT].setChannels(maxChannels);
        outputs[MIX_L_OUTPUT].setChannels(maxChannels);
        outputs[MIX_R_OUTPUT].setChannels(maxChannels);

        // Process each polyphonic channel
        for (int c = 0; c < maxChannels; c++) {
            float sendAL = 0.0f, sendAR = 0.0f;
            float sendBL = 0.0f, sendBR = 0.0f;
            float mixL = 0.0f, mixR = 0.0f;

            // Process each input channel
            for (int i = 0; i < 8; ++i) {
                int leftChannels = inputs[CH1_L_INPUT + i * 2].getChannels();
                int rightChannels = inputs[CH1_R_INPUT + i * 2].getChannels();

                float inputL = 0.0f, inputR = 0.0f;

                // Get input voltages (use channel 0 if current channel doesn't exist)
                if (inputs[CH1_L_INPUT + i * 2].isConnected()) {
                    int useChan = (c < leftChannels) ? c : 0;
                    inputL = inputs[CH1_L_INPUT + i * 2].getPolyVoltage(useChan);
                }

                if (inputs[CH1_R_INPUT + i * 2].isConnected()) {
                    int useChan = (c < rightChannels) ? c : 0;
                    inputR = inputs[CH1_R_INPUT + i * 2].getPolyVoltage(useChan);
                } else if (inputs[CH1_L_INPUT + i * 2].isConnected()) {
                    // If only left is connected, use it for right as well
                    inputR = inputL;
                }

                float sendALevel = params[CH1_SEND_A_PARAM + i * 2].getValue();
                float sendBLevel = params[CH1_SEND_B_PARAM + i * 2].getValue();

                sendAL += inputL * sendALevel;
                sendAR += inputR * sendALevel;
                sendBL += inputL * sendBLevel;
                sendBR += inputR * sendBLevel;
            }

            // Set send outputs
            outputs[SEND_A_L_OUTPUT].setVoltage(sendAL, c);
            outputs[SEND_A_R_OUTPUT].setVoltage(sendAR, c);
            outputs[SEND_B_L_OUTPUT].setVoltage(sendBL, c);
            outputs[SEND_B_R_OUTPUT].setVoltage(sendBR, c);

            // Process returns and chain
            float returnAL = 0.0f, returnAR = 0.0f;
            float returnBL = 0.0f, returnBR = 0.0f;
            float chainL = 0.0f, chainR = 0.0f;

            if (inputs[RETURN_A_L_INPUT].isConnected()) {
                int chanCount = inputs[RETURN_A_L_INPUT].getChannels();
                int useChan = (c < chanCount) ? c : 0;
                returnAL = inputs[RETURN_A_L_INPUT].getPolyVoltage(useChan);
            }

            if (inputs[RETURN_A_R_INPUT].isConnected()) {
                int chanCount = inputs[RETURN_A_R_INPUT].getChannels();
                int useChan = (c < chanCount) ? c : 0;
                returnAR = inputs[RETURN_A_R_INPUT].getPolyVoltage(useChan);
            }

            if (inputs[RETURN_B_L_INPUT].isConnected()) {
                int chanCount = inputs[RETURN_B_L_INPUT].getChannels();
                int useChan = (c < chanCount) ? c : 0;
                returnBL = inputs[RETURN_B_L_INPUT].getPolyVoltage(useChan);
            }

            if (inputs[RETURN_B_R_INPUT].isConnected()) {
                int chanCount = inputs[RETURN_B_R_INPUT].getChannels();
                int useChan = (c < chanCount) ? c : 0;
                returnBR = inputs[RETURN_B_R_INPUT].getPolyVoltage(useChan);
            }

            if (inputs[CHAIN_L_INPUT].isConnected()) {
                int chanCount = inputs[CHAIN_L_INPUT].getChannels();
                int useChan = (c < chanCount) ? c : 0;
                chainL = inputs[CHAIN_L_INPUT].getPolyVoltage(useChan);
            }

            if (inputs[CHAIN_R_INPUT].isConnected()) {
                int chanCount = inputs[CHAIN_R_INPUT].getChannels();
                int useChan = (c < chanCount) ? c : 0;
                chainR = inputs[CHAIN_R_INPUT].getPolyVoltage(useChan);
            }

            // Mix outputs
            mixL = returnAL + returnBL + chainL;
            mixR = returnAR + returnBR + chainR;

            outputs[MIX_L_OUTPUT].setVoltage(mixL, c);
            outputs[MIX_R_OUTPUT].setVoltage(mixR, c);
        }
    }

    void processBypass(const ProcessArgs& args) override {
        int chainLeftChannels = inputs[CHAIN_L_INPUT].getChannels();
        int chainRightChannels = inputs[CHAIN_R_INPUT].getChannels();
        int maxChannels = std::max(chainLeftChannels, chainRightChannels);

        outputs[MIX_L_OUTPUT].setChannels(maxChannels);
        outputs[MIX_R_OUTPUT].setChannels(maxChannels);

        for (int c = 0; c < maxChannels; c++) {
            float chainL = (c < chainLeftChannels) ? inputs[CHAIN_L_INPUT].getPolyVoltage(c) : 0.0f;
            float chainR = (c < chainRightChannels) ? inputs[CHAIN_R_INPUT].getPolyVoltage(c) : 0.0f;

            outputs[MIX_L_OUTPUT].setVoltage(chainL, c);
            outputs[MIX_R_OUTPUT].setVoltage(chainR, c);
        }
    }
};

struct YAMANOTEWidget : ModuleWidget {
    YAMANOTEWidget(YAMANOTE* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "YAMANOTE.png")));
        
        box.size = Vec(8 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        float startY = 52;
        float rowHeight = 33;
        
        for (int i = 0; i < 8; ++i) {
            float y = startY + i * rowHeight;
            
            addInput(createInputCentered<PJ301MPort>(Vec(15, y), module, YAMANOTE::CH1_L_INPUT + i * 2));
            addInput(createInputCentered<PJ301MPort>(Vec(45, y), module, YAMANOTE::CH1_R_INPUT + i * 2));
            addParam(createParamCentered<Trimpot>(Vec(75, y), module, YAMANOTE::CH1_SEND_A_PARAM + i * 2));
            addParam(createParamCentered<Trimpot>(Vec(105, y), module, YAMANOTE::CH1_SEND_B_PARAM + i * 2));
        }

        addOutput(createOutputCentered<PJ301MPort>(Vec(15, 315), module, YAMANOTE::SEND_A_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(45, 315), module, YAMANOTE::SEND_A_R_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(75, 315), module, YAMANOTE::SEND_B_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(105, 315), module, YAMANOTE::SEND_B_R_OUTPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(105, 343), module, YAMANOTE::MIX_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(105, 368), module, YAMANOTE::MIX_R_OUTPUT));
        
        addInput(createInputCentered<PJ301MPort>(Vec(15, 343), module, YAMANOTE::CHAIN_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(15, 368), module, YAMANOTE::CHAIN_R_INPUT));
        
        addInput(createInputCentered<PJ301MPort>(Vec(45, 343), module, YAMANOTE::RETURN_A_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 368), module, YAMANOTE::RETURN_A_R_INPUT));
        
        addInput(createInputCentered<PJ301MPort>(Vec(75, 343), module, YAMANOTE::RETURN_B_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(75, 368), module, YAMANOTE::RETURN_B_R_INPUT));
    }
};

Model* modelYAMANOTE = createModel<YAMANOTE, YAMANOTEWidget>("YAMANOTE");