#include "plugin.hpp"

using namespace rack;
using namespace rack::engine;
using namespace rack::math;

struct ChaosGenerator {
    float x = 0.1f;
    float y = 0.1f;
    float z = 0.1f;
    
    void reset() {
        x = 0.1f;
        y = 0.1f;
        z = 0.1f;
    }
    
    float process(float rate) {
        float dt = rate * 0.001f;
        
        float dx = 7.5f * (y - x);
        float dy = x * (30.9f - z) - y;
        float dz = x * y - 1.02f * z;
        
        x += dx * dt;
        y += dy * dt;
        z += dz * dt;
        
        if (std::isnan(x) || std::isnan(y) || std::isnan(z) || 
            std::abs(x) > 100.0f || std::abs(y) > 100.0f || std::abs(z) > 100.0f) {
            reset();
        }
        
        return clamp(x * 0.1f, -1.0f, 1.0f);
    }
};

struct ReverbProcessor {
    static constexpr int COMB_1_SIZE = 1557;
    static constexpr int COMB_2_SIZE = 1617;
    static constexpr int COMB_3_SIZE = 1491;
    static constexpr int COMB_4_SIZE = 1422;
    static constexpr int COMB_5_SIZE = 1277;
    static constexpr int COMB_6_SIZE = 1356;
    static constexpr int COMB_7_SIZE = 1188;
    static constexpr int COMB_8_SIZE = 1116;
    
    float combBuffer1[COMB_1_SIZE];
    float combBuffer2[COMB_2_SIZE];
    float combBuffer3[COMB_3_SIZE];
    float combBuffer4[COMB_4_SIZE];
    float combBuffer5[COMB_5_SIZE];
    float combBuffer6[COMB_6_SIZE];
    float combBuffer7[COMB_7_SIZE];
    float combBuffer8[COMB_8_SIZE];
    
    int combIndex1 = 0, combIndex2 = 0, combIndex3 = 0, combIndex4 = 0;
    int combIndex5 = 0, combIndex6 = 0, combIndex7 = 0, combIndex8 = 0;
    
    float combLp1 = 0.0f, combLp2 = 0.0f, combLp3 = 0.0f, combLp4 = 0.0f;
    float combLp5 = 0.0f, combLp6 = 0.0f, combLp7 = 0.0f, combLp8 = 0.0f;
    
    static constexpr int ALLPASS_1_SIZE = 556;
    static constexpr int ALLPASS_2_SIZE = 441;
    static constexpr int ALLPASS_3_SIZE = 341;
    static constexpr int ALLPASS_4_SIZE = 225;
    
    float allpassBuffer1[ALLPASS_1_SIZE];
    float allpassBuffer2[ALLPASS_2_SIZE];
    float allpassBuffer3[ALLPASS_3_SIZE];
    float allpassBuffer4[ALLPASS_4_SIZE];
    
    int allpassIndex1 = 0, allpassIndex2 = 0, allpassIndex3 = 0, allpassIndex4 = 0;
    
    ReverbProcessor() { reset(); }
    
    void reset() {
        for (int i = 0; i < COMB_1_SIZE; i++) combBuffer1[i] = 0.0f;
        for (int i = 0; i < COMB_2_SIZE; i++) combBuffer2[i] = 0.0f;
        for (int i = 0; i < COMB_3_SIZE; i++) combBuffer3[i] = 0.0f;
        for (int i = 0; i < COMB_4_SIZE; i++) combBuffer4[i] = 0.0f;
        for (int i = 0; i < COMB_5_SIZE; i++) combBuffer5[i] = 0.0f;
        for (int i = 0; i < COMB_6_SIZE; i++) combBuffer6[i] = 0.0f;
        for (int i = 0; i < COMB_7_SIZE; i++) combBuffer7[i] = 0.0f;
        for (int i = 0; i < COMB_8_SIZE; i++) combBuffer8[i] = 0.0f;
        
        for (int i = 0; i < ALLPASS_1_SIZE; i++) allpassBuffer1[i] = 0.0f;
        for (int i = 0; i < ALLPASS_2_SIZE; i++) allpassBuffer2[i] = 0.0f;
        for (int i = 0; i < ALLPASS_3_SIZE; i++) allpassBuffer3[i] = 0.0f;
        for (int i = 0; i < ALLPASS_4_SIZE; i++) allpassBuffer4[i] = 0.0f;
        
        combIndex1 = combIndex2 = combIndex3 = combIndex4 = 0;
        combIndex5 = combIndex6 = combIndex7 = combIndex8 = 0;
        allpassIndex1 = allpassIndex2 = allpassIndex3 = allpassIndex4 = 0;
        
        combLp1 = combLp2 = combLp3 = combLp4 = 0.0f;
        combLp5 = combLp6 = combLp7 = combLp8 = 0.0f;
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
    
    float process(float inputL, float inputR, float grainDensity,
                  float roomSize, float damping, float decay, bool isLeftChannel,
                  bool chaosEnabled, float chaosOutput, float sampleRate) {
        
        float input = isLeftChannel ? inputL : inputR;
        float feedback = 0.5f + decay * 0.485f;
        if (chaosEnabled) {
            feedback += chaosOutput * 0.5f;
            feedback = clamp(feedback, 0.0f, 0.995f);
        }
        
        float dampingCoeff = 0.05f + damping * 0.9f;
        float roomScale = 0.3f + roomSize * 1.4f;
        
        float combOut = 0.0f;
        
        if (isLeftChannel) {
            int roomOffset1 = std::max(0, (int)(roomSize * 400 + chaosOutput * 50));
            int roomOffset2 = std::max(0, (int)(roomSize * 350 + chaosOutput * 40));
            int roomOffset3 = std::max(0, (int)(roomSize * 300 + chaosOutput * 60));
            int roomOffset4 = std::max(0, (int)(roomSize * 500 + chaosOutput * 70));

            int readIdx1 = ((combIndex1 - roomOffset1) % COMB_1_SIZE + COMB_1_SIZE) % COMB_1_SIZE;
            int readIdx2 = ((combIndex2 - roomOffset2) % COMB_2_SIZE + COMB_2_SIZE) % COMB_2_SIZE;
            int readIdx3 = ((combIndex3 - roomOffset3) % COMB_3_SIZE + COMB_3_SIZE) % COMB_3_SIZE;
            int readIdx4 = ((combIndex4 - roomOffset4) % COMB_4_SIZE + COMB_4_SIZE) % COMB_4_SIZE;
            
            float roomInput = input * roomScale;
            combOut += processComb(roomInput, combBuffer1, COMB_1_SIZE, combIndex1, feedback, combLp1, dampingCoeff);
            combOut += processComb(roomInput, combBuffer2, COMB_2_SIZE, combIndex2, feedback, combLp2, dampingCoeff);
            combOut += processComb(roomInput, combBuffer3, COMB_3_SIZE, combIndex3, feedback, combLp3, dampingCoeff);
            combOut += processComb(roomInput, combBuffer4, COMB_4_SIZE, combIndex4, feedback, combLp4, dampingCoeff);
            
            combOut += combBuffer1[readIdx1] * roomSize * 0.15f;
            combOut += combBuffer2[readIdx2] * roomSize * 0.12f;
        } else {
            int roomOffset5 = std::max(0, (int)(roomSize * 380 + chaosOutput * 45));
            int roomOffset6 = std::max(0, (int)(roomSize * 420 + chaosOutput * 55));
            int roomOffset7 = std::max(0, (int)(roomSize * 280 + chaosOutput * 35));
            int roomOffset8 = std::max(0, (int)(roomSize * 460 + chaosOutput * 65));

            int readIdx5 = ((combIndex5 - roomOffset5) % COMB_5_SIZE + COMB_5_SIZE) % COMB_5_SIZE;
            int readIdx6 = ((combIndex6 - roomOffset6) % COMB_6_SIZE + COMB_6_SIZE) % COMB_6_SIZE;
            int readIdx7 = ((combIndex7 - roomOffset7) % COMB_7_SIZE + COMB_7_SIZE) % COMB_7_SIZE;
            int readIdx8 = ((combIndex8 - roomOffset8) % COMB_8_SIZE + COMB_8_SIZE) % COMB_8_SIZE;
            
            float roomInput = input * roomScale;
            combOut += processComb(roomInput, combBuffer5, COMB_5_SIZE, combIndex5, feedback, combLp5, dampingCoeff);
            combOut += processComb(roomInput, combBuffer6, COMB_6_SIZE, combIndex6, feedback, combLp6, dampingCoeff);
            combOut += processComb(roomInput, combBuffer7, COMB_7_SIZE, combIndex7, feedback, combLp7, dampingCoeff);
            combOut += processComb(roomInput, combBuffer8, COMB_8_SIZE, combIndex8, feedback, combLp8, dampingCoeff);
            
            combOut += combBuffer5[readIdx5] * roomSize * 0.13f;
            combOut += combBuffer6[readIdx6] * roomSize * 0.11f;
        }
        
        combOut *= 0.25f;
        
        float diffused = combOut;
        diffused = processAllpass(diffused, allpassBuffer1, ALLPASS_1_SIZE, allpassIndex1, 0.5f);
        diffused = processAllpass(diffused, allpassBuffer2, ALLPASS_2_SIZE, allpassIndex2, 0.5f);
        diffused = processAllpass(diffused, allpassBuffer3, ALLPASS_3_SIZE, allpassIndex3, 0.5f);
        diffused = processAllpass(diffused, allpassBuffer4, ALLPASS_4_SIZE, allpassIndex4, 0.5f);
        
        return diffused;
    }
};

struct GrainProcessor {
    static constexpr int GRAIN_BUFFER_SIZE = 8192;
    float grainBuffer[GRAIN_BUFFER_SIZE];
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
    dsp::SchmittTrigger grainTrigger;
    
    void reset() {
        for (int i = 0; i < GRAIN_BUFFER_SIZE; i++) {
            grainBuffer[i] = 0.0f;
        }
        grainWriteIndex = 0;
        
        for (int i = 0; i < MAX_GRAINS; i++) {
            grains[i].active = false;
        }
        phase = 0.0f;
    }
    
    float process(float input, float grainSize, float density, float position, 
                  bool chaosEnabled, float chaosOutput, float sampleRate) {
        
        grainBuffer[grainWriteIndex] = input;
        grainWriteIndex = (grainWriteIndex + 1) % GRAIN_BUFFER_SIZE;
        
        float grainSizeMs = grainSize * 99.0f + 1.0f;
        float grainSamples = (grainSizeMs / 1000.0f) * sampleRate;
        
        float densityValue = density;
        if (chaosEnabled) {
            densityValue += chaosOutput * 0.3f;
        }
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
                        if (random::uniform() < 0.3f) {
                            grains[i].direction = -1.0f;
                        } else {
                            grains[i].direction = 1.0f;
                        }
                        
                        if (densityValue > 0.7f && random::uniform() < 0.2f) {
                            grains[i].pitch = random::uniform() < 0.5f ? 0.5f : 2.0f;
                        } else {
                            grains[i].pitch = 1.0f;
                        }
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
                
                if (envPhase >= 1.0f) {
                    grains[i].active = false;
                    continue;
                }
                
                float env = 0.5f * (1.0f - cos(envPhase * 2.0f * M_PI));
                
                int readPos = (int)grains[i].position;
                readPos = (readPos + GRAIN_BUFFER_SIZE) % GRAIN_BUFFER_SIZE;
                
                float sample = grainBuffer[readPos];
                output += sample * env;
                
                grains[i].position += grains[i].direction * grains[i].pitch;
                grains[i].envelope += 1.0f;
                activeGrains++;
            }
        }
        
        if (activeGrains > 0) {
            output /= sqrt(activeGrains);
        }
        
        return output;
    }
};

struct EllenRipley : rack::engine::Module {
    enum ParamIds {
        DELAY_TIME_L_PARAM,
        DELAY_TIME_R_PARAM,
        DELAY_FEEDBACK_PARAM,
        DELAY_CHAOS_PARAM,
        WET_DRY_PARAM,
        CHAOS_RATE_PARAM,
        GRAIN_SIZE_PARAM,
        GRAIN_DENSITY_PARAM,
        GRAIN_POSITION_PARAM,
        GRAIN_CHAOS_PARAM,
        GRAIN_WET_DRY_PARAM,
        REVERB_ROOM_SIZE_PARAM,
        REVERB_DAMPING_PARAM,
        REVERB_DECAY_PARAM,
        REVERB_CHAOS_PARAM,
        REVERB_WET_DRY_PARAM,
        CHAOS_AMOUNT_PARAM,
        CHAOS_SHAPE_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        LEFT_AUDIO_INPUT,
        RIGHT_AUDIO_INPUT,
        DELAY_TIME_L_CV_INPUT,
        DELAY_TIME_R_CV_INPUT,
        DELAY_FEEDBACK_CV_INPUT,
        GRAIN_SIZE_CV_INPUT,
        GRAIN_DENSITY_CV_INPUT,
        GRAIN_POSITION_CV_INPUT,
        REVERB_ROOM_SIZE_CV_INPUT,
        REVERB_DAMPING_CV_INPUT,
        REVERB_DECAY_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        LEFT_AUDIO_OUTPUT,
        RIGHT_AUDIO_OUTPUT,
        CHAOS_CV_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        DELAY_CHAOS_LIGHT,
        GRAIN_CHAOS_LIGHT,
        REVERB_CHAOS_LIGHT,
        CHAOS_SHAPE_LIGHT,
        NUM_LIGHTS
    };
    
    static constexpr int DELAY_BUFFER_SIZE = 96000;
    float leftDelayBuffer[DELAY_BUFFER_SIZE];
    float rightDelayBuffer[DELAY_BUFFER_SIZE];
    int delayWriteIndex = 0;
    
    ChaosGenerator chaosGen;
    GrainProcessor leftGrainProcessor;
    GrainProcessor rightGrainProcessor;
    ReverbProcessor leftReverbProcessor;
    ReverbProcessor rightReverbProcessor;
    
    bool delayChaosMod = false;
    bool grainChaosMod = false;
    bool reverbChaosMod = false;
    
    EllenRipley() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        
        configParam(DELAY_TIME_L_PARAM, 0.001f, 2.0f, 0.25f, "Delay Time L", " s");
        configParam(DELAY_TIME_R_PARAM, 0.001f, 2.0f, 0.25f, "Delay Time R", " s");
        configParam(DELAY_FEEDBACK_PARAM, 0.0f, 0.95f, 0.3f, "Feedback", "%", 0.f, 100.f);
        configParam(DELAY_CHAOS_PARAM, 0.0f, 1.0f, 0.0f, "Delay Chaos");
        configParam(WET_DRY_PARAM, 0.0f, 1.0f, 0.0f, "Delay Wet/Dry", "%", 0.f, 100.f);
        configParam(CHAOS_RATE_PARAM, 0.0f, 1.0f, 0.01f, "Chaos Rate");
        
        configParam(GRAIN_SIZE_PARAM, 0.0f, 1.0f, 0.3f, "Grain Size");
        configParam(GRAIN_DENSITY_PARAM, 0.0f, 1.0f, 0.4f, "Grain Density/Glitch");
        configParam(GRAIN_POSITION_PARAM, 0.0f, 1.0f, 0.5f, "Grain Position/Chaos");
        configParam(GRAIN_CHAOS_PARAM, 0.0f, 1.0f, 0.0f, "Grain Chaos");
        configParam(GRAIN_WET_DRY_PARAM, 0.0f, 1.0f, 0.0f, "Gratch Wet/Dry", "%", 0.f, 100.f);
        
        configParam(REVERB_ROOM_SIZE_PARAM, 0.0f, 1.0f, 0.5f, "Room Size");
        configParam(REVERB_DAMPING_PARAM, 0.0f, 1.0f, 0.4f, "Damping");
        configParam(REVERB_DECAY_PARAM, 0.0f, 1.0f, 0.6f, "Decay");
        configParam(REVERB_CHAOS_PARAM, 0.0f, 1.0f, 0.0f, "Reverb Chaos");
        configParam(REVERB_WET_DRY_PARAM, 0.0f, 1.0f, 0.0f, "Reverb Wet/Dry", "%", 0.f, 100.f);
        configParam(CHAOS_AMOUNT_PARAM, 0.0f, 1.0f, 1.0f, "Chaos Amount");
        configParam(CHAOS_SHAPE_PARAM, 0.0f, 1.0f, 0.0f, "Chaos Shape");
        
        configInput(LEFT_AUDIO_INPUT, "Left Audio");
        configInput(RIGHT_AUDIO_INPUT, "Right Audio");
        configInput(DELAY_TIME_L_CV_INPUT, "Delay Time L CV");
        configInput(DELAY_TIME_R_CV_INPUT, "Delay Time R CV");
        configInput(DELAY_FEEDBACK_CV_INPUT, "Feedback CV");
        configInput(GRAIN_SIZE_CV_INPUT, "Grain Size CV");
        configInput(GRAIN_DENSITY_CV_INPUT, "Grain Density CV");
        configInput(GRAIN_POSITION_CV_INPUT, "Grain Position CV");
        configInput(REVERB_ROOM_SIZE_CV_INPUT, "Reverb Room Size CV");
        configInput(REVERB_DAMPING_CV_INPUT, "Reverb Damping CV");
        configInput(REVERB_DECAY_CV_INPUT, "Reverb Decay CV");
        
        configOutput(LEFT_AUDIO_OUTPUT, "Left Audio");
        configOutput(RIGHT_AUDIO_OUTPUT, "Right Audio");
        configOutput(CHAOS_CV_OUTPUT, "Chaos CV");
        
        configLight(DELAY_CHAOS_LIGHT, "Delay Chaos");
        configLight(GRAIN_CHAOS_LIGHT, "Grain Chaos");
        configLight(REVERB_CHAOS_LIGHT, "Reverb Chaos");
        configLight(CHAOS_SHAPE_LIGHT, "Chaos Shape");
        
        for (int i = 0; i < DELAY_BUFFER_SIZE; i++) {
            leftDelayBuffer[i] = 0.0f;
            rightDelayBuffer[i] = 0.0f;
        }
    }
    
    void onReset() override {
        chaosGen.reset();
        leftGrainProcessor.reset();
        rightGrainProcessor.reset();
        leftReverbProcessor.reset();
        rightReverbProcessor.reset();
        for (int i = 0; i < DELAY_BUFFER_SIZE; i++) {
            leftDelayBuffer[i] = 0.0f;
            rightDelayBuffer[i] = 0.0f;
        }
        delayWriteIndex = 0;
    }
    
    void process(const ProcessArgs& args) override {
        delayChaosMod = params[DELAY_CHAOS_PARAM].getValue() > 0.5f;
        grainChaosMod = params[GRAIN_CHAOS_PARAM].getValue() > 0.5f;
        reverbChaosMod = params[REVERB_CHAOS_PARAM].getValue() > 0.5f;
        
        float chaosRateParam = params[CHAOS_RATE_PARAM].getValue();
        bool chaosStep = params[CHAOS_SHAPE_PARAM].getValue() > 0.5f;
        float chaosRate;
        
        if (chaosStep) {
            chaosRate = 1.0f + chaosRateParam * 9.0f;
        } else {
            chaosRate = 0.01f + chaosRateParam * 0.99f;
        }
        float chaosAmount = params[CHAOS_AMOUNT_PARAM].getValue();
        float chaosRaw = chaosGen.process(chaosRate) * chaosAmount;
        
        float chaosOutput;
        if (chaosStep) {
            static float lastStep = 0.0f;
            static float stepPhase = 0.0f;
            float stepRate = chaosRate * 10.0f;
            stepPhase += stepRate / args.sampleRate;
            if (stepPhase >= 1.0f) {
                lastStep = chaosRaw;
                stepPhase = 0.0f;
            }
            chaosOutput = lastStep;
        } else {
            chaosOutput = chaosRaw;
        }
        
        outputs[CHAOS_CV_OUTPUT].setVoltage(chaosOutput * 5.0f);
        
        lights[DELAY_CHAOS_LIGHT].setBrightness(delayChaosMod ? 1.0f : 0.0f);
        lights[GRAIN_CHAOS_LIGHT].setBrightness(grainChaosMod ? 1.0f : 0.0f);
        lights[REVERB_CHAOS_LIGHT].setBrightness(reverbChaosMod ? 1.0f : 0.0f);
        lights[CHAOS_SHAPE_LIGHT].setBrightness(chaosStep ? 1.0f : 0.0f);
        
        float leftInput = inputs[LEFT_AUDIO_INPUT].getVoltage();
        float rightInput = inputs[RIGHT_AUDIO_INPUT].isConnected() ? inputs[RIGHT_AUDIO_INPUT].getVoltage() : leftInput;
        
        float delayTimeL = params[DELAY_TIME_L_PARAM].getValue();
        if (inputs[DELAY_TIME_L_CV_INPUT].isConnected()) {
            float cv = inputs[DELAY_TIME_L_CV_INPUT].getVoltage();
            delayTimeL += cv * 0.2f;
        }
        if (delayChaosMod) {
            delayTimeL += chaosOutput * 0.1f;
        }
        delayTimeL = clamp(delayTimeL, 0.001f, 2.0f);
        
        float delayTimeR = params[DELAY_TIME_R_PARAM].getValue();
        if (inputs[DELAY_TIME_R_CV_INPUT].isConnected()) {
            float cv = inputs[DELAY_TIME_R_CV_INPUT].getVoltage();
            delayTimeR += cv * 0.2f;
        }
        if (delayChaosMod) {
            delayTimeR += chaosOutput * 0.1f;
        }
        delayTimeR = clamp(delayTimeR, 0.001f, 2.0f);
        
        float feedback = params[DELAY_FEEDBACK_PARAM].getValue();
        if (inputs[DELAY_FEEDBACK_CV_INPUT].isConnected()) {
            float cv = inputs[DELAY_FEEDBACK_CV_INPUT].getVoltage();
            feedback += cv * 0.1f;
        }
        if (delayChaosMod) {
            feedback += chaosOutput * 0.1f;
        }
        feedback = clamp(feedback, 0.0f, 0.95f);
        
        int delaySamplesL = (int)(delayTimeL * args.sampleRate);
        delaySamplesL = clamp(delaySamplesL, 1, DELAY_BUFFER_SIZE - 1);
        
        int delaySamplesR = (int)(delayTimeR * args.sampleRate);
        delaySamplesR = clamp(delaySamplesR, 1, DELAY_BUFFER_SIZE - 1);
        
        int readIndexL = (delayWriteIndex - delaySamplesL + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
        int readIndexR = (delayWriteIndex - delaySamplesR + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;
        
        float leftDelayedSignal = leftDelayBuffer[readIndexL];
        float rightDelayedSignal = rightDelayBuffer[readIndexR];
        
        float grainSize = params[GRAIN_SIZE_PARAM].getValue();
        if (inputs[GRAIN_SIZE_CV_INPUT].isConnected()) {
            grainSize += inputs[GRAIN_SIZE_CV_INPUT].getVoltage() * 0.1f;
        }
        grainSize = clamp(grainSize, 0.0f, 1.0f);
        
        float grainDensity = params[GRAIN_DENSITY_PARAM].getValue();
        if (inputs[GRAIN_DENSITY_CV_INPUT].isConnected()) {
            grainDensity += inputs[GRAIN_DENSITY_CV_INPUT].getVoltage() * 0.1f;
        }
        grainDensity = clamp(grainDensity, 0.0f, 1.0f);
        
        float grainPosition = params[GRAIN_POSITION_PARAM].getValue();
        if (inputs[GRAIN_POSITION_CV_INPUT].isConnected()) {
            grainPosition += inputs[GRAIN_POSITION_CV_INPUT].getVoltage() * 0.1f;
        }
        grainPosition = clamp(grainPosition, 0.0f, 1.0f);
        
        float reverbRoomSize = params[REVERB_ROOM_SIZE_PARAM].getValue();
        if (inputs[REVERB_ROOM_SIZE_CV_INPUT].isConnected()) {
            reverbRoomSize += inputs[REVERB_ROOM_SIZE_CV_INPUT].getVoltage() * 0.1f;
        }
        reverbRoomSize = clamp(reverbRoomSize, 0.0f, 1.0f);
        
        float reverbDamping = params[REVERB_DAMPING_PARAM].getValue();
        if (inputs[REVERB_DAMPING_CV_INPUT].isConnected()) {
            reverbDamping += inputs[REVERB_DAMPING_CV_INPUT].getVoltage() * 0.1f;
        }
        reverbDamping = clamp(reverbDamping, 0.0f, 1.0f);
        
        float reverbDecay = params[REVERB_DECAY_PARAM].getValue();
        if (inputs[REVERB_DECAY_CV_INPUT].isConnected()) {
            reverbDecay += inputs[REVERB_DECAY_CV_INPUT].getVoltage() * 0.1f;
        }
        reverbDecay = clamp(reverbDecay, 0.0f, 1.0f);
        
        float leftDelayInput = leftInput + leftDelayedSignal * feedback;
        float rightDelayInput = rightInput + rightDelayedSignal * feedback;
        
        leftDelayBuffer[delayWriteIndex] = leftDelayInput;
        rightDelayBuffer[delayWriteIndex] = rightDelayInput;
        delayWriteIndex = (delayWriteIndex + 1) % DELAY_BUFFER_SIZE;
        
        float delayWetDryMix = params[WET_DRY_PARAM].getValue();
        float leftStage1 = leftInput * (1.0f - delayWetDryMix) + leftDelayedSignal * delayWetDryMix;
        float rightStage1 = rightInput * (1.0f - delayWetDryMix) + rightDelayedSignal * delayWetDryMix;
        
        float leftGrainOutput = leftGrainProcessor.process(leftStage1, grainSize, grainDensity, grainPosition, grainChaosMod, chaosOutput, args.sampleRate);
        float rightGrainOutput = rightGrainProcessor.process(rightStage1, grainSize, grainDensity, grainPosition, grainChaosMod, chaosOutput * -1.0f, args.sampleRate);
        
        float grainWetDryMix = params[GRAIN_WET_DRY_PARAM].getValue();
        float leftStage2 = leftStage1 * (1.0f - grainWetDryMix) + leftGrainOutput * grainWetDryMix;
        float rightStage2 = rightStage1 * (1.0f - grainWetDryMix) + rightGrainOutput * grainWetDryMix;
        
        float leftReverbOutput = leftReverbProcessor.process(leftStage2, rightStage2, grainDensity, reverbRoomSize, reverbDamping, reverbDecay, true, reverbChaosMod, chaosOutput, args.sampleRate);
        float rightReverbOutput = rightReverbProcessor.process(leftStage2, rightStage2, grainDensity, reverbRoomSize, reverbDamping, reverbDecay, false, reverbChaosMod, chaosOutput, args.sampleRate);
        
        float reverbWetDryMix = params[REVERB_WET_DRY_PARAM].getValue();
        float leftFinal = leftStage2 * (1.0f - reverbWetDryMix) + leftReverbOutput * reverbWetDryMix;
        float rightFinal = rightStage2 * (1.0f - reverbWetDryMix) + rightReverbOutput * reverbWetDryMix;
        
        float reverbFeedbackAmount = reverbDecay * 0.3f;
        leftDelayBuffer[delayWriteIndex] += leftReverbOutput * reverbFeedbackAmount;
        rightDelayBuffer[delayWriteIndex] += rightReverbOutput * reverbFeedbackAmount;
        
        outputs[LEFT_AUDIO_OUTPUT].setVoltage(leftFinal);
        outputs[RIGHT_AUDIO_OUTPUT].setVoltage(rightFinal);
    }
};

struct EllenRipleyWidget : ModuleWidget {
    EllenRipleyWidget(EllenRipley* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "EllenRipley.png")));
        
        box.size = Vec(8 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        float delayY = 46;
        float x = 1;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, delayY + 22), module, EllenRipley::DELAY_TIME_L_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, delayY + 47), module, EllenRipley::DELAY_TIME_L_CV_INPUT));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, delayY + 22), module, EllenRipley::DELAY_TIME_R_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, delayY + 47), module, EllenRipley::DELAY_TIME_R_CV_INPUT));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, delayY + 22), module, EllenRipley::DELAY_FEEDBACK_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, delayY + 47), module, EllenRipley::DELAY_FEEDBACK_CV_INPUT));
        x += 30;
        
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(x + 12, delayY + 22), module, EllenRipley::DELAY_CHAOS_PARAM, EllenRipley::DELAY_CHAOS_LIGHT));
        
        float grainY = 128;
        x = 1;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, grainY + 22), module, EllenRipley::GRAIN_SIZE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, grainY + 47), module, EllenRipley::GRAIN_SIZE_CV_INPUT));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, grainY + 22), module, EllenRipley::GRAIN_DENSITY_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, grainY + 47), module, EllenRipley::GRAIN_DENSITY_CV_INPUT));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, grainY + 22), module, EllenRipley::GRAIN_POSITION_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, grainY + 47), module, EllenRipley::GRAIN_POSITION_CV_INPUT));
        x += 30;
        
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(x + 12, grainY + 22), module, EllenRipley::GRAIN_CHAOS_PARAM, EllenRipley::GRAIN_CHAOS_LIGHT));
        
        float reverbY = 210;
        x = 1;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, reverbY + 22), module, EllenRipley::REVERB_ROOM_SIZE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, reverbY + 47), module, EllenRipley::REVERB_ROOM_SIZE_CV_INPUT));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, reverbY + 22), module, EllenRipley::REVERB_DAMPING_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, reverbY + 47), module, EllenRipley::REVERB_DAMPING_CV_INPUT));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, reverbY + 22), module, EllenRipley::REVERB_DECAY_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(x + 12, reverbY + 47), module, EllenRipley::REVERB_DECAY_CV_INPUT));
        x += 30;
        
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(x + 12, reverbY + 22), module, EllenRipley::REVERB_CHAOS_PARAM, EllenRipley::REVERB_CHAOS_LIGHT));
        
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(107, 282), module, EllenRipley::CHAOS_SHAPE_PARAM, EllenRipley::CHAOS_SHAPE_LIGHT));
        
        float chaosY = 292;
        x = 1;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, chaosY + 22), module, EllenRipley::CHAOS_RATE_PARAM));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, chaosY + 22), module, EllenRipley::WET_DRY_PARAM));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, chaosY + 22), module, EllenRipley::GRAIN_WET_DRY_PARAM));
        x += 31;
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(x + 12, chaosY + 22), module, EllenRipley::REVERB_WET_DRY_PARAM));
        
        addInput(createInputCentered<PJ301MPort>(Vec(15, 343), module, EllenRipley::LEFT_AUDIO_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(15, 368), module, EllenRipley::RIGHT_AUDIO_INPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(105, 343), module, EllenRipley::LEFT_AUDIO_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(105, 368), module, EllenRipley::RIGHT_AUDIO_OUTPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(80, 343), module, EllenRipley::CHAOS_CV_OUTPUT));
        addParam(createParamCentered<RoundBlackKnob>(Vec(80, 368), module, EllenRipley::CHAOS_AMOUNT_PARAM));
    }
};

Model* modelEllenRipley = createModel<EllenRipley, EllenRipleyWidget>("EllenRipley");