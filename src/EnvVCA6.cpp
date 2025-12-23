#include "plugin.hpp"
#include <cmath>

// AD/AHR Envelope class (based on ADGenerator)
struct ADEnvelope {
    enum Phase {
        IDLE,
        ATTACK,
        HOLD,    // New hold phase for AHR mode
        DECAY    // Also serves as Release in AHR mode
    };

    Phase phase = IDLE;
    float triggerOutput = 0.0f;
    float followerOutput = 0.0f;
    float attackTime = 0.01f;
    float decayTime = 1.0f;
    float phaseTime = 0.0f;
    float curve = -0.9f;  // Default shape
    float followerState = 0.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    bool ahrMode = false;  // false = AD mode, true = AHR mode
    bool gateHigh = false; // Track gate state for AHR mode

    dsp::SchmittTrigger trigger;

    void reset() {
        phase = IDLE;
        triggerOutput = 0.0f;
        followerOutput = 0.0f;
        phaseTime = 0.0f;
        followerState = 0.0f;
    }

    float applyCurve(float x, float curvature) {
        x = clamp(x, 0.0f, 1.0f);

        if (curvature == 0.0f) {
            return x;
        }

        float k = curvature;
        float abs_x = std::abs(x);
        float denominator = k - 2.0f * k * abs_x + 1.0f;

        if (std::abs(denominator) < 1e-6f) {
            return x;
        }

        return (x - k * x) / denominator;
    }

    float processEnvelopeFollower(float triggerVoltage, float sampleTime, float attackTime, float releaseTime, float curve) {
        attackCoeff = 1.0f - std::exp(-sampleTime / std::max(0.0005f, attackTime * 0.1f));
        releaseCoeff = 1.0f - std::exp(-sampleTime / std::max(0.001f, releaseTime * 0.5f));

        attackCoeff = clamp(attackCoeff, 0.0f, 1.0f);
        releaseCoeff = clamp(releaseCoeff, 0.0f, 1.0f);

        float rectified = std::abs(triggerVoltage) / 10.0f;
        rectified = clamp(rectified, 0.0f, 1.0f);

        float targetCoeff;
        if (rectified > followerState) {
            float progress = attackCoeff;
            targetCoeff = applyCurve(progress, curve);
        } else {
            float progress = releaseCoeff;
            targetCoeff = applyCurve(progress, curve);
        }

        targetCoeff = clamp(targetCoeff, 0.0f, 1.0f);

        followerState += (rectified - followerState) * targetCoeff;
        followerState = clamp(followerState, 0.0f, 1.0f);

        return followerState;
    }

    float processTriggerEnvelope(float triggerVoltage, float sampleTime, float attack, float decay, float curve) {
        bool currentGateHigh = triggerVoltage > 1.0f;

        // Trigger on rising edge (SchmittTrigger uses 0.1V low / 2.0V high thresholds)
        if (trigger.process(triggerVoltage)) {
            phase = ATTACK;
            phaseTime = 0.0f;
        }

        // In AHR mode, detect gate release to start release phase
        if (ahrMode && gateHigh && !currentGateHigh && phase == HOLD) {
            phase = DECAY;  // Start release phase
            phaseTime = 0.0f;
        }
        gateHigh = currentGateHigh;

        switch (phase) {
            case IDLE:
                triggerOutput = 0.0f;
                break;

            case ATTACK:
                phaseTime += sampleTime;
                if (phaseTime >= attack) {
                    if (ahrMode) {
                        phase = HOLD;  // Go to hold phase in AHR mode
                        triggerOutput = 1.0f;
                    } else {
                        phase = DECAY;  // Go directly to decay in AD mode
                        phaseTime = 0.0f;
                    }
                    triggerOutput = 1.0f;
                } else {
                    float t = phaseTime / attack;
                    triggerOutput = applyCurve(t, curve);
                }
                break;

            case HOLD:
                // Stay at full level while gate is high (AHR mode only)
                triggerOutput = 1.0f;
                if (!gateHigh) {
                    phase = DECAY;
                    phaseTime = 0.0f;
                }
                break;

            case DECAY:
                phaseTime += sampleTime;
                if (phaseTime >= decay) {
                    triggerOutput = 0.0f;
                    phase = IDLE;
                    phaseTime = 0.0f;
                } else {
                    float t = phaseTime / decay;
                    triggerOutput = 1.0f - applyCurve(t, curve);
                }
                break;
        }

        return clamp(triggerOutput, 0.0f, 1.0f);
    }

    float process(float sampleTime, float triggerVoltage, float attack, float decay) {
        float attackTime = std::pow(10.0f, (attack - 0.5f) * 6.0f);
        float decayTime = std::pow(10.0f, (decay - 0.5f) * 6.0f);

        attackTime = std::max(0.001f, attackTime);
        decayTime = std::max(0.001f, decayTime);

        // Only use trigger envelope - gate voltage amplitude should NOT affect envelope output
        float triggerEnv = processTriggerEnvelope(triggerVoltage, sampleTime, attackTime, decayTime, curve);

        return triggerEnv;
    }
};


struct EnvVCA6 : Module {

    enum ParamId {
        // Channel 1
        CH1_ATTACK_PARAM,
        CH1_RELEASE_PARAM,
        CH1_OUT_VOL_PARAM,
        CH1_GATE_TRIG_PARAM,
        CH1_SUM_LATCH_PARAM,
        CH1_ENV_MODE_PARAM,  // 0 = AD, 1 = AHR
        // Channel 2
        CH2_ATTACK_PARAM,
        CH2_RELEASE_PARAM,
        CH2_OUT_VOL_PARAM,
        CH2_GATE_TRIG_PARAM,
        CH2_SUM_LATCH_PARAM,
        CH2_ENV_MODE_PARAM,
        // Channel 3
        CH3_ATTACK_PARAM,
        CH3_RELEASE_PARAM,
        CH3_OUT_VOL_PARAM,
        CH3_GATE_TRIG_PARAM,
        CH3_SUM_LATCH_PARAM,
        CH3_ENV_MODE_PARAM,
        // Channel 4
        CH4_ATTACK_PARAM,
        CH4_RELEASE_PARAM,
        CH4_OUT_VOL_PARAM,
        CH4_GATE_TRIG_PARAM,
        CH4_SUM_LATCH_PARAM,
        CH4_ENV_MODE_PARAM,
        // Channel 5
        CH5_ATTACK_PARAM,
        CH5_RELEASE_PARAM,
        CH5_OUT_VOL_PARAM,
        CH5_GATE_TRIG_PARAM,
        CH5_SUM_LATCH_PARAM,
        CH5_ENV_MODE_PARAM,
        // Channel 6
        CH6_ATTACK_PARAM,
        CH6_RELEASE_PARAM,
        CH6_OUT_VOL_PARAM,
        CH6_GATE_TRIG_PARAM,
        CH6_SUM_LATCH_PARAM,
        CH6_ENV_MODE_PARAM,
        GATE_MODE_PARAM, // 0 = full cycle gate, 1 = end of cycle trigger
        PARAMS_LEN
    };
    enum InputId {
        // Channel 1
        CH1_IN_L_INPUT,
        CH1_IN_R_INPUT,
        CH1_GATE_INPUT,
        CH1_VOL_CTRL_INPUT,
        // Channel 2
        CH2_IN_L_INPUT,
        CH2_IN_R_INPUT,
        CH2_GATE_INPUT,
        CH2_VOL_CTRL_INPUT,
        // Channel 3
        CH3_IN_L_INPUT,
        CH3_IN_R_INPUT,
        CH3_GATE_INPUT,
        CH3_VOL_CTRL_INPUT,
        // Channel 4
        CH4_IN_L_INPUT,
        CH4_IN_R_INPUT,
        CH4_GATE_INPUT,
        CH4_VOL_CTRL_INPUT,
        // Channel 5
        CH5_IN_L_INPUT,
        CH5_IN_R_INPUT,
        CH5_GATE_INPUT,
        CH5_VOL_CTRL_INPUT,
        // Channel 6
        CH6_IN_L_INPUT,
        CH6_IN_R_INPUT,
        CH6_GATE_INPUT,
        CH6_VOL_CTRL_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        // Channel 1
        CH1_GATE_OUTPUT,
        CH1_ENV_OUTPUT,
        CH1_OUT_L_OUTPUT,
        CH1_OUT_R_OUTPUT,
        // Channel 2
        CH2_GATE_OUTPUT,
        CH2_ENV_OUTPUT,
        CH2_OUT_L_OUTPUT,
        CH2_OUT_R_OUTPUT,
        // Channel 3
        CH3_GATE_OUTPUT,
        CH3_ENV_OUTPUT,
        CH3_OUT_L_OUTPUT,
        CH3_OUT_R_OUTPUT,
        // Channel 4
        CH4_GATE_OUTPUT,
        CH4_ENV_OUTPUT,
        CH4_OUT_L_OUTPUT,
        CH4_OUT_R_OUTPUT,
        // Channel 5
        CH5_GATE_OUTPUT,
        CH5_ENV_OUTPUT,
        CH5_OUT_L_OUTPUT,
        CH5_OUT_R_OUTPUT,
        // Channel 6
        CH6_GATE_OUTPUT,
        CH6_ENV_OUTPUT,
        CH6_OUT_L_OUTPUT,
        CH6_OUT_R_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        CH1_VCA_LIGHT,
        CH2_VCA_LIGHT,
        CH3_VCA_LIGHT,
        CH4_VCA_LIGHT,
        CH5_VCA_LIGHT,
        CH6_VCA_LIGHT,
        LIGHTS_LEN
    };

    ADEnvelope envelopes[6];
    dsp::SchmittTrigger sumLatchTriggers[6]; // Only for sum latch buttons
    bool gateOutputStates[6] = {false}; // Track gate output states
    bool lastEnvelopeActive[6] = {false}; // Track envelope state for end-of-cycle trigger
    dsp::PulseGenerator endOfCyclePulses[6]; // Generate end-of-cycle triggers
    dsp::PulseGenerator startOfCyclePulses[6]; // Generate start-of-cycle triggers
    bool lastGateHigh[6] = {false}; // Track gate input state for start trigger
    int gateMode = 0; // 0 = full cycle, 1 = end trigger, 2 = start+end

    EnvVCA6() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Configure parameters for all 6 channels (6 params per channel)
        for (int i = 0; i < 6; i++) {
            configParam(CH1_ATTACK_PARAM + i * 6, 0.f, 1.f, 0.1f, string::f("Ch %d Attack", i + 1));
            configParam(CH1_RELEASE_PARAM + i * 6, 0.f, 1.f, 0.5f, string::f("Ch %d Release", i + 1));
            configParam(CH1_OUT_VOL_PARAM + i * 6, 0.f, 1.f, 0.8f, string::f("Ch %d Out Volume", i + 1));
            configParam(CH1_GATE_TRIG_PARAM + i * 6, 0.f, 1.f, 0.f, string::f("Ch %d Manual Gate (Momentary)", i + 1));
            if (i < 5) { // Only CH1-5 have sum buttons
                configParam(CH1_SUM_LATCH_PARAM + i * 6, 0.f, 1.f, 0.f, string::f("Ch %d Sum to Ch6", i + 1));
            } else { // CH6 sum button is disabled/hidden
                configParam(CH1_SUM_LATCH_PARAM + i * 6, 0.f, 1.f, 0.f, "Disabled");
            }
            configParam(CH1_ENV_MODE_PARAM + i * 6, 0.f, 1.f, 0.f, string::f("Ch %d Env Mode (AD/AHR)", i + 1));

            // Configure inputs
            configInput(CH1_IN_L_INPUT + i * 4, string::f("Ch %d In L", i + 1));
            configInput(CH1_IN_R_INPUT + i * 4, string::f("Ch %d In R", i + 1));
            configInput(CH1_GATE_INPUT + i * 4, string::f("Ch %d Gate", i + 1));
            configInput(CH1_VOL_CTRL_INPUT + i * 4, string::f("Ch %d Vol Ctrl", i + 1));

            // Configure outputs
            configOutput(CH1_GATE_OUTPUT + i * 4, string::f("Ch %d Gate", i + 1));

            if (i == 5) { // CH6 special tooltips
                configOutput(CH1_ENV_OUTPUT + i * 4, "Ch 6 Envelope / Sum Envelope");
                configOutput(CH1_OUT_L_OUTPUT + i * 4, "Ch 6 Out L / Sum L");
                configOutput(CH1_OUT_R_OUTPUT + i * 4, "Ch 6 Out R / Sum R");
            } else {
                configOutput(CH1_ENV_OUTPUT + i * 4, string::f("Ch %d Envelope", i + 1));
                configOutput(CH1_OUT_L_OUTPUT + i * 4, string::f("Ch %d Out L", i + 1));
                configOutput(CH1_OUT_R_OUTPUT + i * 4, string::f("Ch %d Out R", i + 1));
            }

            // Configure lights
            configLight(CH1_VCA_LIGHT + i, string::f("Ch %d VCA Active", i + 1));
        }

        // Hidden parameter for gate output mode (now stored in module variable)
        configParam(GATE_MODE_PARAM, 0.f, 2.f, 0.f, "Gate Output Mode");
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "gateMode", json_integer(gateMode));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* gateModeJ = json_object_get(rootJ, "gateMode");
        if (gateModeJ) {
            gateMode = json_integer_value(gateModeJ);
        }
    }

    void process(const ProcessArgs& args) override {
        for (int i = 0; i < 6; i++) {
            // Get parameters for this channel (6 params per channel)
            float attackParam = params[CH1_ATTACK_PARAM + i * 6].getValue();
            float releaseParam = params[CH1_RELEASE_PARAM + i * 6].getValue();
            float outVolParam = params[CH1_OUT_VOL_PARAM + i * 6].getValue();
            bool ahrMode = params[CH1_ENV_MODE_PARAM + i * 6].getValue() > 0.5f;

            // Set envelope mode
            envelopes[i].ahrMode = ahrMode;

            // Get inputs
            float inL = inputs[CH1_IN_L_INPUT + i * 4].getVoltage();
            float inR = inputs[CH1_IN_R_INPUT + i * 4].getVoltage();
            float gateIn = inputs[CH1_GATE_INPUT + i * 4].getVoltage();
            float volCtrl = inputs[CH1_VOL_CTRL_INPUT + i * 4].getVoltage();

            // Mono-to-stereo: if only L input connected, copy to R
            if (!inputs[CH1_IN_R_INPUT + i * 4].isConnected() && inputs[CH1_IN_L_INPUT + i * 4].isConnected()) {
                inR = inL;
            }

            // Manual gate logic: momentary (only while button pressed)
            bool manualGateActive = params[CH1_GATE_TRIG_PARAM + i * 6].getValue() > 0.5f;

            // Combine gate sources (input + momentary manual gate)
            float combinedGate = std::max(gateIn, manualGateActive ? 10.f : 0.f);

            // Process envelope
            float envelopeOutput = envelopes[i].process(args.sampleTime, combinedGate, attackParam, releaseParam);

            // Apply VCA (envelope controls volume)
            float vcaGain = envelopeOutput;

            // Apply volume control CV (0-10V range) - default to 1.0 if not connected
            float volCtrlGain = 1.f;
            if (inputs[CH1_VOL_CTRL_INPUT + i * 4].isConnected()) {
                volCtrlGain = clamp(volCtrl / 10.f, 0.f, 1.f);
            }
            vcaGain *= volCtrlGain;

            // Apply output volume knob
            vcaGain *= outVolParam;

            // Process audio
            float outL = inL * vcaGain;
            float outR = inR * vcaGain;

            // Gate output logic (three modes)
            float gateOutputVoltage = 0.f;

            if (gateMode == 0) {
                // Mode 0: Full cycle gate (gate high during entire envelope)
                if (combinedGate > 1.f) {
                    gateOutputStates[i] = true;
                }
                if (envelopes[i].phase == ADEnvelope::IDLE && envelopeOutput <= 0.001f) {
                    gateOutputStates[i] = false;
                }
                gateOutputVoltage = gateOutputStates[i] ? 10.f : 0.f;
            } else if (gateMode == 1) {
                // Mode 1: End of cycle trigger only
                bool envelopeActive = (envelopeOutput > 0.001f);
                if (lastEnvelopeActive[i] && !envelopeActive) {
                    // Envelope just finished - trigger pulse
                    endOfCyclePulses[i].trigger(1e-3f); // 1ms pulse
                }
                lastEnvelopeActive[i] = envelopeActive;
                gateOutputVoltage = endOfCyclePulses[i].process(args.sampleTime) ? 10.f : 0.f;
            } else {
                // Mode 2: Start + End of cycle triggers
                bool gateHigh = (combinedGate > 1.f);
                bool envelopeActive = (envelopeOutput > 0.001f);

                // Detect rising edge of gate (start of cycle)
                if (gateHigh && !lastGateHigh[i]) {
                    startOfCyclePulses[i].trigger(1e-3f); // 1ms pulse at start
                }
                lastGateHigh[i] = gateHigh;

                // Detect end of envelope (end of cycle)
                if (lastEnvelopeActive[i] && !envelopeActive) {
                    endOfCyclePulses[i].trigger(1e-3f); // 1ms pulse at end
                }
                lastEnvelopeActive[i] = envelopeActive;

                // Output either trigger (start OR end)
                bool startTrigger = startOfCyclePulses[i].process(args.sampleTime);
                bool endTrigger = endOfCyclePulses[i].process(args.sampleTime);
                gateOutputVoltage = (startTrigger || endTrigger) ? 10.f : 0.f;
            }

            // Set outputs
            outputs[CH1_GATE_OUTPUT + i * 4].setVoltage(gateOutputVoltage);
            outputs[CH1_ENV_OUTPUT + i * 4].setVoltage(envelopeOutput * 10.f);
            outputs[CH1_OUT_L_OUTPUT + i * 4].setVoltage(outL);
            outputs[CH1_OUT_R_OUTPUT + i * 4].setVoltage(outR);

            // VCA light shows current VCA level
            lights[CH1_VCA_LIGHT + i].setBrightness(vcaGain);
        }

        // Sum outputs to CH6 (if sum latch is enabled) - ADD to CH6, don't replace
        float sumL = 0.f;
        float sumR = 0.f;
        float sumEnv = 0.f;
        int sumCount = 0;

        for (int i = 0; i < 5; i++) { // Only sum first 5 channels (CH1-CH5)
            bool sumEnabled = params[CH1_SUM_LATCH_PARAM + i * 6].getValue() > 0.5f;
            if (sumEnabled) {
                sumL += outputs[CH1_OUT_L_OUTPUT + i * 4].getVoltage() * 0.3f; // Scale for mixing
                sumR += outputs[CH1_OUT_R_OUTPUT + i * 4].getVoltage() * 0.3f;

                // Sum envelopes with RMS-like scaling to prevent overload
                float envValue = outputs[CH1_ENV_OUTPUT + i * 4].getVoltage() / 10.f; // Convert to 0-1
                sumEnv += envValue * envValue; // Square for RMS
                sumCount++;
            }
        }

        // ADD sum to CH6 outputs (not replace) if any channels are summed
        if (sumCount > 0) {
            // Add summed audio to CH6's own output
            float ch6L = outputs[CH1_OUT_L_OUTPUT + 5 * 4].getVoltage();
            float ch6R = outputs[CH1_OUT_R_OUTPUT + 5 * 4].getVoltage();
            outputs[CH1_OUT_L_OUTPUT + 5 * 4].setVoltage(ch6L + sumL);
            outputs[CH1_OUT_R_OUTPUT + 5 * 4].setVoltage(ch6R + sumR);

            // Add RMS envelope sum to CH6's envelope
            float ch6Env = outputs[CH1_ENV_OUTPUT + 5 * 4].getVoltage();
            float rmsEnv = std::sqrt(sumEnv / sumCount) * 10.f; // Back to 0-10V range
            outputs[CH1_ENV_OUTPUT + 5 * 4].setVoltage(std::max(ch6Env, rmsEnv)); // Use max to preserve CH6 envelope
        }
    }
};

struct EnvVCA6Widget : ModuleWidget {
    EnvVCA6Widget(EnvVCA6* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "EnvVCA6.png")));
        box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        // Layout parameters (matching VCV version exactly)
        float channelHeight = 48.f;
        float startY = 53.f;

        for (int i = 0; i < 6; i++) {
            float y = startY + i * channelHeight;

            // Input jacks (left side) - matching VCV positions
            addInput(createInputCentered<PJ301MPort>(Vec(15, y), module, EnvVCA6::CH1_IN_L_INPUT + i * 4));
            addInput(createInputCentered<PJ301MPort>(Vec(15, y + 24), module, EnvVCA6::CH1_IN_R_INPUT + i * 4));
            addInput(createInputCentered<PJ301MPort>(Vec(45, y), module, EnvVCA6::CH1_GATE_INPUT + i * 4));
            addInput(createInputCentered<PJ301MPort>(Vec(45, y + 24), module, EnvVCA6::CH1_VOL_CTRL_INPUT + i * 4));

            // Control knobs (center) - matching VCV positions
            addParam(createParamCentered<RoundBlackKnob>(Vec(75, y), module, EnvVCA6::CH1_ATTACK_PARAM + i * 6));
            addParam(createParamCentered<RoundBlackKnob>(Vec(105, y), module, EnvVCA6::CH1_RELEASE_PARAM + i * 6));
            addParam(createParamCentered<RoundBlackKnob>(Vec(135, y), module, EnvVCA6::CH1_OUT_VOL_PARAM + i * 6));

            // Buttons - VCV uses box.pos (top-left), MetaModule uses centered
            addParam(createParamCentered<VCVButton>(Vec(75, y + 20), module, EnvVCA6::CH1_GATE_TRIG_PARAM + i * 6));
            addParam(createParamCentered<VCVButton>(Vec(95, y + 20), module, EnvVCA6::CH1_ENV_MODE_PARAM + i * 6));
            addParam(createParamCentered<VCVButton>(Vec(115, y + 20), module, EnvVCA6::CH1_SUM_LATCH_PARAM + i * 6));

            // VCA activity light - matching VCV position
            addChild(createLightCentered<MediumLight<GreenLight>>(Vec(135, y + 20), module, EnvVCA6::CH1_VCA_LIGHT + i));

            // Output jacks (right side) - matching VCV positions
            addOutput(createOutputCentered<PJ301MPort>(Vec(165, y), module, EnvVCA6::CH1_GATE_OUTPUT + i * 4));
            addOutput(createOutputCentered<PJ301MPort>(Vec(165, y + 24), module, EnvVCA6::CH1_ENV_OUTPUT + i * 4));
        }

        // Audio outputs at bottom (white section) - matching VCV positions
        float outputXPositions[6] = {15, 45, 75, 105, 135, 165};
        for (int i = 0; i < 6; i++) {
            float x = outputXPositions[i];
            addOutput(createOutputCentered<PJ301MPort>(Vec(x, 343), module, EnvVCA6::CH1_OUT_L_OUTPUT + i * 4));
            addOutput(createOutputCentered<PJ301MPort>(Vec(x, 368), module, EnvVCA6::CH1_OUT_R_OUTPUT + i * 4));
        }
    }

    void appendContextMenu(Menu* menu) override {
        EnvVCA6* module = dynamic_cast<EnvVCA6*>(this->module);
        if (!module) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Gate Output Mode"));

        struct GateModeItem : MenuItem {
            EnvVCA6* module;
            int mode;

            void onAction(const event::Action& e) override {
                module->gateMode = mode;
            }

            void step() override {
                rightText = (module->gateMode == mode) ? "✓" : "";
                MenuItem::step();
            }
        };

        GateModeItem* fullCycleItem = createMenuItem<GateModeItem>("Full Cycle Gate");
        fullCycleItem->module = module;
        fullCycleItem->mode = 0;
        menu->addChild(fullCycleItem);

        GateModeItem* endTriggerItem = createMenuItem<GateModeItem>("End of Cycle Trigger");
        endTriggerItem->module = module;
        endTriggerItem->mode = 1;
        menu->addChild(endTriggerItem);

        GateModeItem* startEndItem = createMenuItem<GateModeItem>("Start + End Triggers");
        startEndItem->module = module;
        startEndItem->mode = 2;
        menu->addChild(startEndItem);
    }
};

Model* modelEnvVCA6 = createModel<EnvVCA6, EnvVCA6Widget>("EnvVCA6");
