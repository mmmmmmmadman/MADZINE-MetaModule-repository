#include "plugin.hpp"

struct RunnerChaosGenerator {
    float x = 0.1f, y = 0.1f, z = 0.1f;
    void reset() { x = 0.1f; y = 0.1f; z = 0.1f; }
    float process(float rate) {
        float dt = rate * 0.001f;
        float dx = 7.5f * (y - x); float dy = x * (30.9f - z) - y; float dz = x * y - 1.02f * z;
        x += dx * dt; y += dy * dt; z += dz * dt;
        if (std::isnan(x) || std::isnan(y) || std::isnan(z) ||
            std::abs(x) > 100.0f || std::abs(y) > 100.0f || std::abs(z) > 100.0f) reset();
        return clamp(x * 0.1f, -1.0f, 1.0f);
    }
};

struct Runner : Module {
    enum ParamIds {
        TIME_L_PARAM, TIME_R_PARAM, FEEDBACK_PARAM,
        MIX_PARAM, CHAOS_PARAM, RATE_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        LEFT_INPUT, RIGHT_INPUT,
        TIME_L_CV_INPUT, TIME_R_CV_INPUT, FEEDBACK_CV_INPUT,
        MIX_CV_INPUT, CHAOS_CV_INPUT, RATE_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        LEFT_OUTPUT, RIGHT_OUTPUT, CHAOS_OUTPUT, SH_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds { NUM_LIGHTS };

    static constexpr int DELAY_BUFFER_SIZE = 48000;
    float leftDelayBuffer[DELAY_BUFFER_SIZE] = {};
    float rightDelayBuffer[DELAY_BUFFER_SIZE] = {};
    int delayWriteIndex = 0;

    RunnerChaosGenerator chaosGen;
    float lastSHValue = 0.0f;
    float shPhase = 0.0f;

    Runner() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(TIME_L_PARAM, 0.001f, 1.0f, 0.25f, "Time L", " s");
        configParam(TIME_R_PARAM, 0.001f, 1.0f, 0.25f, "Time R", " s");
        configParam(FEEDBACK_PARAM, 0.0f, 0.95f, 0.3f, "Feedback", "%", 0.f, 100.f);
        configParam(MIX_PARAM, 0.0f, 1.0f, 0.5f, "Mix", "%", 0.f, 100.f);
        configParam(CHAOS_PARAM, 0.0f, 1.0f, 0.0f, "Chaos", "%", 0.f, 100.f);
        configParam(RATE_PARAM, 0.01f, 2.0f, 0.5f, "Rate", "x");
        configInput(LEFT_INPUT, "Left Audio"); configInput(RIGHT_INPUT, "Right Audio");
        configInput(TIME_L_CV_INPUT, "Time L CV"); configInput(TIME_R_CV_INPUT, "Time R CV");
        configInput(FEEDBACK_CV_INPUT, "Feedback CV"); configInput(MIX_CV_INPUT, "Mix CV");
        configInput(CHAOS_CV_INPUT, "Chaos CV"); configInput(RATE_CV_INPUT, "Rate CV");
        configOutput(LEFT_OUTPUT, "Left Audio"); configOutput(RIGHT_OUTPUT, "Right Audio");
        configOutput(CHAOS_OUTPUT, "Chaos CV"); configOutput(SH_OUTPUT, "Sample & Hold CV");
    }

    void onReset() override {
        chaosGen.reset();
        for (int i = 0; i < DELAY_BUFFER_SIZE; i++) { leftDelayBuffer[i] = 0.0f; rightDelayBuffer[i] = 0.0f; }
        delayWriteIndex = 0;
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate <= 0) return;

        float chaosAmount = params[CHAOS_PARAM].getValue();
        if (inputs[CHAOS_CV_INPUT].isConnected()) chaosAmount += inputs[CHAOS_CV_INPUT].getVoltage() * 0.1f;
        chaosAmount = clamp(chaosAmount, 0.0f, 1.0f);

        float chaosRate = params[RATE_PARAM].getValue();
        if (inputs[RATE_CV_INPUT].isConnected()) chaosRate += inputs[RATE_CV_INPUT].getVoltage() * 0.2f;
        chaosRate = clamp(chaosRate, 0.01f, 2.0f);

        bool chaosEnabled = chaosAmount > 0.0f;
        float chaosRaw = 0.0f, chaosSH = 0.0f;
        if (chaosEnabled) {
            chaosRaw = chaosGen.process(chaosRate) * chaosAmount;
            shPhase += chaosRate * 10.0f / args.sampleRate;
            if (shPhase >= 1.0f) { lastSHValue = chaosRaw; shPhase = 0.0f; }
            chaosSH = lastSHValue;
        }
        outputs[CHAOS_OUTPUT].setVoltage(chaosRaw * 5.0f);
        outputs[SH_OUTPUT].setVoltage(chaosSH * 5.0f);

        float leftInput = inputs[LEFT_INPUT].getVoltage();
        float rightInput = inputs[RIGHT_INPUT].isConnected() ? inputs[RIGHT_INPUT].getVoltage() : leftInput;
        if (!std::isfinite(leftInput)) leftInput = 0.0f;
        if (!std::isfinite(rightInput)) rightInput = 0.0f;

        float timeL = params[TIME_L_PARAM].getValue();
        if (inputs[TIME_L_CV_INPUT].isConnected()) timeL += inputs[TIME_L_CV_INPUT].getVoltage() * 0.2f;
        if (chaosEnabled) timeL += chaosRaw * 0.1f;
        timeL = clamp(timeL, 0.001f, 1.0f);

        float timeR = params[TIME_R_PARAM].getValue();
        if (inputs[TIME_R_CV_INPUT].isConnected()) timeR += inputs[TIME_R_CV_INPUT].getVoltage() * 0.2f;
        if (chaosEnabled) timeR += chaosRaw * 0.1f;
        timeR = clamp(timeR, 0.001f, 1.0f);

        float feedback = params[FEEDBACK_PARAM].getValue();
        if (inputs[FEEDBACK_CV_INPUT].isConnected()) feedback += inputs[FEEDBACK_CV_INPUT].getVoltage() * 0.1f;
        if (chaosEnabled) feedback += chaosRaw * 0.1f;
        feedback = clamp(feedback, 0.0f, 0.95f);

        int delaySamplesL = clamp((int)(timeL * args.sampleRate), 1, DELAY_BUFFER_SIZE - 1);
        int delaySamplesR = clamp((int)(timeR * args.sampleRate), 1, DELAY_BUFFER_SIZE - 1);

        int readIndexL = (delayWriteIndex - delaySamplesL + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
        int readIndexR = (delayWriteIndex - delaySamplesR + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;

        float leftDelayed = leftDelayBuffer[readIndexL];
        float rightDelayed = rightDelayBuffer[readIndexR];

        leftDelayBuffer[delayWriteIndex] = leftInput + leftDelayed * feedback;
        rightDelayBuffer[delayWriteIndex] = rightInput + rightDelayed * feedback;
        delayWriteIndex = (delayWriteIndex + 1) % DELAY_BUFFER_SIZE;

        float mix = params[MIX_PARAM].getValue();
        if (inputs[MIX_CV_INPUT].isConnected()) mix += inputs[MIX_CV_INPUT].getVoltage() * 0.1f;
        mix = clamp(mix, 0.0f, 1.0f);

        float leftOut = leftInput * (1.0f - mix) + leftDelayed * mix;
        float rightOut = rightInput * (1.0f - mix) + rightDelayed * mix;
        if (!std::isfinite(leftOut)) leftOut = 0.0f;
        if (!std::isfinite(rightOut)) rightOut = 0.0f;

        outputs[LEFT_OUTPUT].setVoltage(leftOut);
        outputs[RIGHT_OUTPUT].setVoltage(rightOut);
    }
};

struct RunnerWidget : ModuleWidget {
    RunnerWidget(Runner* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "Runner.png")));
        box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        float leftX = 15.0f, rightX = 45.0f;

        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 72), module, Runner::TIME_L_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 72), module, Runner::TIME_R_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 117), module, Runner::FEEDBACK_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 117), module, Runner::MIX_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 162), module, Runner::CHAOS_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 162), module, Runner::RATE_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 197), module, Runner::TIME_L_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 197), module, Runner::TIME_R_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 232), module, Runner::FEEDBACK_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 232), module, Runner::MIX_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 267), module, Runner::CHAOS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 267), module, Runner::RATE_CV_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(Vec(leftX, 302), module, Runner::CHAOS_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 302), module, Runner::SH_OUTPUT));

        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 343), module, Runner::LEFT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 343), module, Runner::LEFT_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 368), module, Runner::RIGHT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 368), module, Runner::RIGHT_OUTPUT));
    }
};

Model* modelRunner = createModel<Runner, RunnerWidget>("Runner");
