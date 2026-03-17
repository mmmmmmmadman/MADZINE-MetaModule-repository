#include "plugin.hpp"

// ChaosGenerator - Lorenz Attractor
struct OvomorphChaosGenerator {
    float x = 0.1f, y = 0.1f, z = 0.1f;
    void reset() { x = 0.1f; y = 0.1f; z = 0.1f; }
    float process(float rate) {
        float dt = rate * 0.001f;
        float dx = 7.5f * (y - x);
        float dy = x * (30.9f - z) - y;
        float dz = x * y - 1.02f * z;
        x += dx * dt; y += dy * dt; z += dz * dt;
        if (std::isnan(x) || std::isnan(y) || std::isnan(z) ||
            std::abs(x) > 100.0f || std::abs(y) > 100.0f || std::abs(z) > 100.0f) reset();
        return clamp(x * 0.1f, -1.0f, 1.0f);
    }
};

// ReverbProcessor - Freeverb style (from RipleyDSP.hpp, 9-param version)
struct OvomorphReverbProcessor {
    static constexpr int COMB_1_SIZE = 1557, COMB_2_SIZE = 1617, COMB_3_SIZE = 1491, COMB_4_SIZE = 1422;
    static constexpr int COMB_5_SIZE = 1277, COMB_6_SIZE = 1356, COMB_7_SIZE = 1188, COMB_8_SIZE = 1116;
    float combBuffer1[COMB_1_SIZE] = {}, combBuffer2[COMB_2_SIZE] = {}, combBuffer3[COMB_3_SIZE] = {}, combBuffer4[COMB_4_SIZE] = {};
    float combBuffer5[COMB_5_SIZE] = {}, combBuffer6[COMB_6_SIZE] = {}, combBuffer7[COMB_7_SIZE] = {}, combBuffer8[COMB_8_SIZE] = {};
    int combIndex1=0,combIndex2=0,combIndex3=0,combIndex4=0,combIndex5=0,combIndex6=0,combIndex7=0,combIndex8=0;
    float combLp1=0,combLp2=0,combLp3=0,combLp4=0,combLp5=0,combLp6=0,combLp7=0,combLp8=0;
    float hpState = 0.0f;

    static constexpr int ALLPASS_1_SIZE = 556, ALLPASS_2_SIZE = 441, ALLPASS_3_SIZE = 341, ALLPASS_4_SIZE = 225;
    float allpassBuffer1[ALLPASS_1_SIZE] = {}, allpassBuffer2[ALLPASS_2_SIZE] = {}, allpassBuffer3[ALLPASS_3_SIZE] = {}, allpassBuffer4[ALLPASS_4_SIZE] = {};
    int allpassIndex1=0,allpassIndex2=0,allpassIndex3=0,allpassIndex4=0;

    void reset() {
        for (int i=0;i<COMB_1_SIZE;i++) combBuffer1[i]=0; for (int i=0;i<COMB_2_SIZE;i++) combBuffer2[i]=0;
        for (int i=0;i<COMB_3_SIZE;i++) combBuffer3[i]=0; for (int i=0;i<COMB_4_SIZE;i++) combBuffer4[i]=0;
        for (int i=0;i<COMB_5_SIZE;i++) combBuffer5[i]=0; for (int i=0;i<COMB_6_SIZE;i++) combBuffer6[i]=0;
        for (int i=0;i<COMB_7_SIZE;i++) combBuffer7[i]=0; for (int i=0;i<COMB_8_SIZE;i++) combBuffer8[i]=0;
        for (int i=0;i<ALLPASS_1_SIZE;i++) allpassBuffer1[i]=0; for (int i=0;i<ALLPASS_2_SIZE;i++) allpassBuffer2[i]=0;
        for (int i=0;i<ALLPASS_3_SIZE;i++) allpassBuffer3[i]=0; for (int i=0;i<ALLPASS_4_SIZE;i++) allpassBuffer4[i]=0;
        combIndex1=combIndex2=combIndex3=combIndex4=combIndex5=combIndex6=combIndex7=combIndex8=0;
        allpassIndex1=allpassIndex2=allpassIndex3=allpassIndex4=0;
        combLp1=combLp2=combLp3=combLp4=combLp5=combLp6=combLp7=combLp8=0;
        hpState=0;
    }

    float processComb(float input, float* buffer, int size, int& index, float feedback, float& lp, float damping) {
        float output = buffer[index];
        lp = lp + (output - lp) * damping;
        buffer[index] = input + lp * feedback;
        index = (index + 1) % size;
        return output;
    }
    float processAllpass(float input, float* buffer, int size, int& index, float gain) {
        float delayed = buffer[index];
        float output = -input * gain + delayed;
        buffer[index] = input + delayed * gain;
        index = (index + 1) % size;
        return output;
    }

    float process(float inputL, float inputR, float roomSize, float damping, float decay,
                  bool isLeftChannel, bool chaosEnabled, float chaosOutput, float sampleRate) {
        float input = isLeftChannel ? inputL : inputR;
        float feedback = 0.5f + decay * 0.485f;
        if (chaosEnabled) { feedback += chaosOutput * 0.5f; feedback = clamp(feedback, 0.0f, 0.995f); }
        float dampingCoeff = 0.05f + damping * 0.9f;
        float roomScale = 0.3f + roomSize * 1.4f;
        float combOut = 0.0f;

        if (isLeftChannel) {
            int ro1 = std::max(0, (int)(roomSize * 400 + chaosOutput * 50));
            int ro2 = std::max(0, (int)(roomSize * 350 + chaosOutput * 40));
            int ri1 = ((combIndex1 - ro1) % COMB_1_SIZE + COMB_1_SIZE) % COMB_1_SIZE;
            int ri2 = ((combIndex2 - ro2) % COMB_2_SIZE + COMB_2_SIZE) % COMB_2_SIZE;
            float roomInput = input * roomScale;
            combOut += processComb(roomInput, combBuffer1, COMB_1_SIZE, combIndex1, feedback, combLp1, dampingCoeff);
            combOut += processComb(roomInput, combBuffer2, COMB_2_SIZE, combIndex2, feedback, combLp2, dampingCoeff);
            combOut += processComb(roomInput, combBuffer3, COMB_3_SIZE, combIndex3, feedback, combLp3, dampingCoeff);
            combOut += processComb(roomInput, combBuffer4, COMB_4_SIZE, combIndex4, feedback, combLp4, dampingCoeff);
            combOut += combBuffer1[ri1] * roomSize * 0.15f;
            combOut += combBuffer2[ri2] * roomSize * 0.12f;
        } else {
            int ro5 = std::max(0, (int)(roomSize * 380 + chaosOutput * 45));
            int ro6 = std::max(0, (int)(roomSize * 420 + chaosOutput * 55));
            int ri5 = ((combIndex5 - ro5) % COMB_5_SIZE + COMB_5_SIZE) % COMB_5_SIZE;
            int ri6 = ((combIndex6 - ro6) % COMB_6_SIZE + COMB_6_SIZE) % COMB_6_SIZE;
            float roomInput = input * roomScale;
            combOut += processComb(roomInput, combBuffer5, COMB_5_SIZE, combIndex5, feedback, combLp5, dampingCoeff);
            combOut += processComb(roomInput, combBuffer6, COMB_6_SIZE, combIndex6, feedback, combLp6, dampingCoeff);
            combOut += processComb(roomInput, combBuffer7, COMB_7_SIZE, combIndex7, feedback, combLp7, dampingCoeff);
            combOut += processComb(roomInput, combBuffer8, COMB_8_SIZE, combIndex8, feedback, combLp8, dampingCoeff);
            combOut += combBuffer5[ri5] * roomSize * 0.13f;
            combOut += combBuffer6[ri6] * roomSize * 0.11f;
        }
        combOut *= 0.25f;
        float diffused = combOut;
        diffused = processAllpass(diffused, allpassBuffer1, ALLPASS_1_SIZE, allpassIndex1, 0.5f);
        diffused = processAllpass(diffused, allpassBuffer2, ALLPASS_2_SIZE, allpassIndex2, 0.5f);
        diffused = processAllpass(diffused, allpassBuffer3, ALLPASS_3_SIZE, allpassIndex3, 0.5f);
        diffused = processAllpass(diffused, allpassBuffer4, ALLPASS_4_SIZE, allpassIndex4, 0.5f);

        float hpCutoff = clamp(100.0f / (sampleRate * 0.5f), 0.001f, 0.1f);
        hpState += (diffused - hpState) * hpCutoff;
        return diffused - hpState;
    }
};

struct Ovomorph : Module {
    enum ParamIds {
        ROOM_PARAM, TONE_PARAM, DECAY_PARAM,
        MIX_PARAM, CHAOS_PARAM, RATE_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        LEFT_INPUT, RIGHT_INPUT,
        ROOM_CV_INPUT, TONE_CV_INPUT, DECAY_CV_INPUT,
        MIX_CV_INPUT, CHAOS_CV_INPUT, RATE_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        LEFT_OUTPUT, RIGHT_OUTPUT, CHAOS_OUTPUT, SH_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds { NUM_LIGHTS };

    OvomorphChaosGenerator chaosGen;
    OvomorphReverbProcessor leftReverbProcessor;
    OvomorphReverbProcessor rightReverbProcessor;
    float lastSHValue = 0.0f;
    float shPhase = 0.0f;

    Ovomorph() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(ROOM_PARAM, 0.0f, 1.0f, 0.5f, "Room");
        configParam(TONE_PARAM, 0.0f, 1.0f, 0.4f, "Tone");
        configParam(DECAY_PARAM, 0.0f, 1.0f, 0.6f, "Decay");
        configParam(MIX_PARAM, 0.0f, 1.0f, 0.5f, "Mix", "%", 0.f, 100.f);
        configParam(CHAOS_PARAM, 0.0f, 1.0f, 0.0f, "Chaos", "%", 0.f, 100.f);
        configParam(RATE_PARAM, 0.01f, 2.0f, 0.5f, "Rate", "x");
        configInput(LEFT_INPUT, "Left Audio"); configInput(RIGHT_INPUT, "Right Audio");
        configInput(ROOM_CV_INPUT, "Room CV"); configInput(TONE_CV_INPUT, "Tone CV");
        configInput(DECAY_CV_INPUT, "Decay CV"); configInput(MIX_CV_INPUT, "Mix CV");
        configInput(CHAOS_CV_INPUT, "Chaos CV"); configInput(RATE_CV_INPUT, "Rate CV");
        configOutput(LEFT_OUTPUT, "Left Audio"); configOutput(RIGHT_OUTPUT, "Right Audio");
        configOutput(CHAOS_OUTPUT, "Chaos CV"); configOutput(SH_OUTPUT, "Sample & Hold CV");
    }

    void onReset() override {
        chaosGen.reset(); leftReverbProcessor.reset(); rightReverbProcessor.reset();
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

        float roomSize = params[ROOM_PARAM].getValue();
        if (inputs[ROOM_CV_INPUT].isConnected()) roomSize += inputs[ROOM_CV_INPUT].getVoltage() * 0.1f;
        roomSize = clamp(roomSize, 0.0f, 1.0f);

        float damping = params[TONE_PARAM].getValue();
        if (inputs[TONE_CV_INPUT].isConnected()) damping += inputs[TONE_CV_INPUT].getVoltage() * 0.1f;
        damping = clamp(damping, 0.0f, 1.0f);

        float decay = params[DECAY_PARAM].getValue();
        if (inputs[DECAY_CV_INPUT].isConnected()) decay += inputs[DECAY_CV_INPUT].getVoltage() * 0.1f;
        decay = clamp(decay, 0.0f, 1.0f);

        float leftReverb = leftReverbProcessor.process(leftInput, rightInput, roomSize, damping, decay, true, chaosEnabled, chaosRaw, args.sampleRate);
        float rightReverb = rightReverbProcessor.process(leftInput, rightInput, roomSize, damping, decay, false, chaosEnabled, chaosRaw, args.sampleRate);

        float mix = params[MIX_PARAM].getValue();
        if (inputs[MIX_CV_INPUT].isConnected()) mix += inputs[MIX_CV_INPUT].getVoltage() * 0.1f;
        mix = clamp(mix, 0.0f, 1.0f);

        float leftOut = leftInput * (1.0f - mix) + leftReverb * mix;
        float rightOut = rightInput * (1.0f - mix) + rightReverb * mix;
        if (!std::isfinite(leftOut)) leftOut = 0.0f;
        if (!std::isfinite(rightOut)) rightOut = 0.0f;

        outputs[LEFT_OUTPUT].setVoltage(leftOut);
        outputs[RIGHT_OUTPUT].setVoltage(rightOut);
    }
};

struct OvomorphWidget : ModuleWidget {
    OvomorphWidget(Ovomorph* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "Ovomorph.png")));
        box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        float leftX = 15.0f, rightX = 45.0f;

        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 72), module, Ovomorph::ROOM_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 72), module, Ovomorph::TONE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 117), module, Ovomorph::DECAY_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 117), module, Ovomorph::MIX_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(leftX, 162), module, Ovomorph::CHAOS_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(rightX, 162), module, Ovomorph::RATE_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 197), module, Ovomorph::ROOM_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 197), module, Ovomorph::TONE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 232), module, Ovomorph::DECAY_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 232), module, Ovomorph::MIX_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 267), module, Ovomorph::CHAOS_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(rightX, 267), module, Ovomorph::RATE_CV_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(Vec(leftX, 302), module, Ovomorph::CHAOS_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 302), module, Ovomorph::SH_OUTPUT));

        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 343), module, Ovomorph::LEFT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 343), module, Ovomorph::LEFT_OUTPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(leftX, 368), module, Ovomorph::RIGHT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(rightX, 368), module, Ovomorph::RIGHT_OUTPUT));
    }
};

Model* modelOvomorph = createModel<Ovomorph, OvomorphWidget>("Ovomorph");
