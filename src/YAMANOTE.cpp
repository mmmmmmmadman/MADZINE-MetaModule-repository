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
        float sendAL = 0.0f, sendAR = 0.0f;
        float sendBL = 0.0f, sendBR = 0.0f;
        float mixL = 0.0f, mixR = 0.0f;
        
        for (int i = 0; i < 8; ++i) {
            float inputL = inputs[CH1_L_INPUT + i * 2].getVoltage();
            float inputR = inputs[CH1_R_INPUT + i * 2].getVoltage();
            
            if (inputs[CH1_L_INPUT + i * 2].isConnected() && !inputs[CH1_R_INPUT + i * 2].isConnected()) {
                inputR = inputL;
            }
            
            float sendALevel = params[CH1_SEND_A_PARAM + i * 2].getValue();
            float sendBLevel = params[CH1_SEND_B_PARAM + i * 2].getValue();
            
            sendAL += inputL * sendALevel;
            sendAR += inputR * sendALevel;
            sendBL += inputL * sendBLevel;
            sendBR += inputR * sendBLevel;

        }
        
        outputs[SEND_A_L_OUTPUT].setVoltage(sendAL);
        outputs[SEND_A_R_OUTPUT].setVoltage(sendAR);
        outputs[SEND_B_L_OUTPUT].setVoltage(sendBL);
        outputs[SEND_B_R_OUTPUT].setVoltage(sendBR);
        
        float returnAL = inputs[RETURN_A_L_INPUT].getVoltage();
        float returnAR = inputs[RETURN_A_R_INPUT].getVoltage();
        float returnBL = inputs[RETURN_B_L_INPUT].getVoltage();
        float returnBR = inputs[RETURN_B_R_INPUT].getVoltage();
        
        float chainL = inputs[CHAIN_L_INPUT].getVoltage();
        float chainR = inputs[CHAIN_R_INPUT].getVoltage();
        
        mixL += returnAL + returnBL + chainL;
        mixR += returnAR + returnBR + chainR;
        
        outputs[MIX_L_OUTPUT].setVoltage(mixL);
        outputs[MIX_R_OUTPUT].setVoltage(mixR);
    }
    
    void processBypass(const ProcessArgs& args) override {
        float chainL = inputs[CHAIN_L_INPUT].getVoltage();
        float chainR = inputs[CHAIN_R_INPUT].getVoltage();
        
        outputs[MIX_L_OUTPUT].setVoltage(chainL);
        outputs[MIX_R_OUTPUT].setVoltage(chainR);
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