#include "plugin.hpp"

struct DensityParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        float value = getValue();
        int steps, primaryKnobs;
        
        if (value < 0.2f) {
            steps = 8 + (int)(value * 20);
            primaryKnobs = 2;
        } else if (value < 0.4f) {
            steps = 12 + (int)((value - 0.2f) * 40);
            primaryKnobs = 3;
        } else if (value < 0.6f) {
            steps = 20 + (int)((value - 0.4f) * 40);
            primaryKnobs = 4;
        } else {
            steps = 28 + (int)((value - 0.6f) * 50.1f);
            primaryKnobs = 5;
        }
        steps = clamp(steps, 8, 48);
        
        return string::f("%d knobs, %d steps", primaryKnobs, steps);
    }
};

struct DivMultParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        if (value > 0) {
            return string::f("%dx", value + 1);
        } else if (value < 0) {
            return string::f("1/%dx", -value + 1);
        } else {
            return "1x";
        }
    }
};

void generateMADDYPlusEuclideanRhythm(bool pattern[], int length, int fill, int shift) {
    for (int i = 0; i < 32; ++i) {
        pattern[i] = false;
    }
    if (fill == 0 || length == 0) return;
    if (fill > length) fill = length;

    shift = shift % length;
    if (shift < 0) shift += length;

    for (int i = 0; i < fill; ++i) {
        int index = (int)std::floor((float)i * length / fill);
        pattern[index] = true;
    }
    
    if (shift > 0) {
        bool temp[32];
        for (int i = 0; i < length; ++i) {
            temp[i] = pattern[i];
        }
        for (int i = 0; i < length; ++i) {
            int newIndex = (i + shift) % length;
            pattern[newIndex] = temp[i];
        }
    }
}

struct MADDYPlus : Module {
    enum ParamId {
        FREQ_PARAM,
        SWING_PARAM,
        LENGTH_PARAM,
        DECAY_PARAM,
        TRACK1_FILL_PARAM,
        TRACK1_DIVMULT_PARAM,
        TRACK2_FILL_PARAM,
        TRACK2_DIVMULT_PARAM,
        TRACK3_FILL_PARAM,
        TRACK3_DIVMULT_PARAM,
        K1_PARAM, K2_PARAM, K3_PARAM, K4_PARAM, K5_PARAM,
        MODE_PARAM, DENSITY_PARAM, CHAOS_PARAM, CLOCK_SOURCE_PARAM,
        MANUAL_RESET_PARAM,
        CH2_CLOCK_SOURCE_PARAM,
        CH2_MODE_PARAM,
        CH2_DENSITY_PARAM,
        CH2_CVD_ATTEN_PARAM,
        CH2_STEP_DELAY_PARAM,
        CH3_CLOCK_SOURCE_PARAM,
        CH3_MODE_PARAM,
        CH3_DENSITY_PARAM,
        CH3_CVD_ATTEN_PARAM,
        CH3_STEP_DELAY_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CH2_CV_INPUT,
        CH3_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        CLK_OUTPUT,
        RESET_OUTPUT,
        TRACK1_OUTPUT,
        TRACK2_OUTPUT,
        TRACK3_OUTPUT,
        CHAIN_12_OUTPUT,
        CHAIN_23_OUTPUT,
        CHAIN_123_OUTPUT,
        CV_OUTPUT,
        TRIG_OUTPUT,
        CH2_CV_OUTPUT,
        CH2_TRIG_OUTPUT,
        CH3_CV_OUTPUT,
        CH3_TRIG_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        MODE_LIGHT_RED,
        MODE_LIGHT_GREEN,
        MODE_LIGHT_BLUE,
        MANUAL_RESET_LIGHT,
        LIGHTS_LEN
    };

    float phase = 0.0f;
    float swingPhase = 0.0f;
    dsp::PulseGenerator clockPulse;
    bool isSwingBeat = false;

    struct TrackState {
        int divMultValue = 0;
        int division = 1;
        int multiplication = 1;
        float dividedClockSeconds = 0.5f;
        float multipliedClockSeconds = 0.5f;
        float dividedProgressSeconds = 0.0f;
        float gateSeconds = 0.0f;
        int dividerCount = 0;
        bool shouldStep = false;
        bool prevMultipliedGate = false;

        int currentStep = 0;
        int length = 16;
        int fill = 4;
        int shift = 0;
        bool pattern[32];
        bool gateState = false;
        dsp::PulseGenerator trigPulse;
        dsp::PulseGenerator patternTrigPulse;

        enum Phase {
            IDLE,
            ATTACK,
            DECAY
        };

        Phase envelopePhase = IDLE;
        float envelopeOutput = 0.0f;
        float envelopePhaseTime = 0.0f;
        float attackTime = 0.006f;
        float decayTime = 1.0f;
        float curve = 0.0f;
        float lastDecayParam = -1.0f;
        float currentDecayTime = 1.0f;
        float lastUsedDecayParam = 0.3f;
        bool justTriggered = false;

        void reset() {
            dividedProgressSeconds = 0.0f;
            dividerCount = 0;
            shouldStep = false;
            prevMultipliedGate = false;
            currentStep = 0;
            shift = 0;
            for (int i = 0; i < 32; ++i) {
                pattern[i] = false;
            }
            gateState = false;
            envelopePhase = IDLE;
            envelopeOutput = 0.0f;
            envelopePhaseTime = 0.0f;
            lastDecayParam = -1.0f;
            currentDecayTime = 1.0f;
            lastUsedDecayParam = 0.3f;
            justTriggered = false;
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

        void updateDivMult(int divMultParam) {
            divMultValue = divMultParam;
            if (divMultValue > 0) {
                division = 1;
                multiplication = divMultValue + 1;
            } else if (divMultValue < 0) {
                division = -divMultValue + 1;
                multiplication = 1;
            } else {
                division = 1;
                multiplication = 1;
            }
        }

        bool processClockDivMult(bool globalClock, float globalClockSeconds, float sampleTime) {
            dividedClockSeconds = globalClockSeconds * (float)division;
            multipliedClockSeconds = dividedClockSeconds / (float)multiplication;
            gateSeconds = std::max(0.001f, multipliedClockSeconds * 0.5f);

            if (globalClock) {
                if (dividerCount < 1) {
                    dividedProgressSeconds = 0.0f;
                } else {
                    dividedProgressSeconds += sampleTime;
                }
                ++dividerCount;
                if (dividerCount >= division) {
                    dividerCount = 0;
                }
            } else {
                dividedProgressSeconds += sampleTime;
            }

            shouldStep = false;
            if (dividedProgressSeconds < dividedClockSeconds) {
                float multipliedProgressSeconds = dividedProgressSeconds / multipliedClockSeconds;
                multipliedProgressSeconds -= (float)(int)multipliedProgressSeconds;
                multipliedProgressSeconds *= multipliedClockSeconds;

                bool currentMultipliedGate = multipliedProgressSeconds <= gateSeconds;

                if (currentMultipliedGate && !prevMultipliedGate) {
                    shouldStep = true;
                }
                prevMultipliedGate = currentMultipliedGate;
            }

            return shouldStep;
        }

        void stepTrack() {
            currentStep = (currentStep + 1) % length;
            gateState = pattern[currentStep];
            if (gateState) {
                trigPulse.trigger(0.001f);
                envelopePhase = ATTACK;
                envelopePhaseTime = 0.0f;
                justTriggered = true;
            }
        }

        float processEnvelope(float sampleTime, float decayParam) {
            if (envelopePhase == ATTACK && envelopePhaseTime == 0.0f) {
                float sqrtDecay = std::pow(decayParam, 0.33f);
                float mappedDecay = rescale(sqrtDecay, 0.0f, 1.0f, 0.0f, 0.8f);
                curve = rescale(decayParam, 0.0f, 1.0f, -0.8f, -0.45f);
                currentDecayTime = std::pow(10.0f, (mappedDecay - 0.8f) * 5.0f);
                currentDecayTime = std::max(0.01f, currentDecayTime);
                lastUsedDecayParam = decayParam;
            }

            switch (envelopePhase) {
                case IDLE:
                    envelopeOutput = 0.0f;
                    break;

                case ATTACK:
                    envelopePhaseTime += sampleTime;
                    if (envelopePhaseTime >= attackTime) {
                        envelopePhase = DECAY;
                        envelopePhaseTime = 0.0f;
                        envelopeOutput = 1.0f;
                    } else {
                        float t = envelopePhaseTime / attackTime;
                        envelopeOutput = applyCurve(t, curve);
                    }
                    break;

                case DECAY:
                    envelopePhaseTime += sampleTime;
                    if (envelopePhaseTime >= currentDecayTime) {
                        envelopeOutput = 0.0f;
                        envelopePhase = IDLE;
                        envelopePhaseTime = 0.0f;
                    } else {
                        float t = envelopePhaseTime / currentDecayTime;
                        envelopeOutput = 1.0f - applyCurve(t, curve);
                    }
                    break;
            }

            envelopeOutput = clamp(envelopeOutput, 0.0f, 1.0f);
            return envelopeOutput * 10.0f;
        }
    };
    TrackState tracks[3];

    struct ChainedSequence {
        int currentTrackIndex = 0;
        int trackIndices[4];
        int trackCount = 0;
        int globalClockCount = 0;
        int trackStartClock[3] = {0, 0, 0};
        dsp::PulseGenerator chainTrigPulse;
        dsp::PulseGenerator clockPulse;

        ChainedSequence() {
            for (int i = 0; i < 4; ++i) {
                trackIndices[i] = -1;
            }
        }
        
        void setTrackIndices(int indices[], int count) {
            trackCount = count;
            for (int i = 0; i < count && i < 4; ++i) {
                trackIndices[i] = indices[i];
            }
        }
        
        void reset() {
            currentTrackIndex = 0;
            globalClockCount = 0;
            for (int i = 0; i < 3; ++i) {
                trackStartClock[i] = 0;
            }
            chainTrigPulse.reset();
            clockPulse.reset();
        }
        
        int calculateTrackCycleClock(const TrackState& track) {
            return track.length * track.division / track.multiplication;
        }
        
        float processStep(TrackState tracks[], float sampleTime, bool globalClockTriggered, float decayParam, bool& chainTrigger) {
            chainTrigger = false;
            if (trackCount == 0) return 0.0f;
        
            if (globalClockTriggered) {
                globalClockCount++;
            }
        
            if (currentTrackIndex >= trackCount) {
                currentTrackIndex = 0;
            }
        
            int activeTrackIdx = trackIndices[currentTrackIndex];
            if (activeTrackIdx < 0 || activeTrackIdx >= 3) {
                return 0.0f;
            }
        
            TrackState& activeTrack = tracks[activeTrackIdx];
            int trackCycleClock = calculateTrackCycleClock(activeTrack);
            int elapsedClock = globalClockCount - trackStartClock[activeTrackIdx];
        
            if (elapsedClock >= trackCycleClock) {
                currentTrackIndex++;
                if (currentTrackIndex >= trackCount) {
                    currentTrackIndex = 0;
                }
                activeTrackIdx = trackIndices[currentTrackIndex];
                trackStartClock[activeTrackIdx] = globalClockCount;
                chainTrigger = true;
                chainTrigPulse.trigger(0.001f);
            }
        
            chainTrigger = chainTrigger || chainTrigPulse.process(sampleTime) > 0.0f;
            if (tracks[activeTrackIdx].trigPulse.process(sampleTime) > 0.0f) {
                clockPulse.trigger(0.001f);
            }

            return tracks[activeTrackIdx].envelopeOutput * 10.0f;
        }
    };
    ChainedSequence chain12, chain23, chain123;

    static const int CH2_MAX_DELAY = 8;
    static const int CH3_MAX_DELAY = 8;
    static const int CH2_CVD_BUFFER_SIZE = 192000;
    static const int CH3_CVD_BUFFER_SIZE = 192000;

    struct CVSequencer {
        int currentStep = 0;
        int sequenceLength = 16;
        int stepToKnobMapping[64];
        float previousVoltage = -999.0f;
        int modeValue = 1;
        int clockSourceValue = 0;
        float densityValue = 0.5f;
        int stepDelayCounter = 0;
        int stepDelayValue = 0;
        
        float cvcBuffer[CH2_CVD_BUFFER_SIZE];
        int cvcWriteIndex = 0;
        float cvcDelayTime = 0.0f;
        float cvcAttenuation = 1.0f;
        
        dsp::PulseGenerator gateOutPulse;
        dsp::SchmittTrigger modeTrigger;

        void reset() {
            currentStep = 0;
            previousVoltage = -999.0f;
            stepDelayCounter = 0;
            cvcWriteIndex = 0;
            for (int i = 0; i < CH2_CVD_BUFFER_SIZE; ++i) {
                cvcBuffer[i] = 0.0f;
            }
            for (int i = 0; i < 64; i++) {
                stepToKnobMapping[i] = 0;
            }
            generateMapping();
        }

        void generateMapping() {
            float chaos = 0.0f;
            
            if (densityValue < 0.2f) {
                sequenceLength = 8 + (int)(densityValue * 20);
            } else if (densityValue < 0.4f) {
                sequenceLength = 12 + (int)((densityValue - 0.2f) * 40);
            } else if (densityValue < 0.6f) {
                sequenceLength = 20 + (int)((densityValue - 0.4f) * 40);
            } else {
                sequenceLength = 28 + (int)((densityValue - 0.6f) * 50.1f);
            }
            sequenceLength = clamp(sequenceLength, 8, 48);
            
            int primaryKnobs = (densityValue < 0.2f) ? 2 : (densityValue < 0.4f) ? 3 : (densityValue < 0.6f) ? 4 : 5;
            
            for (int i = 0; i < 64; i++) stepToKnobMapping[i] = 0;
            
            switch (modeValue) {
                case 0:
                    for (int i = 0; i < sequenceLength; i++) {
                        stepToKnobMapping[i] = i % primaryKnobs;
                    }
                    break;
                case 1: {
                    int minimalistPattern[32] = {0,1,2,0,1,2,3,4,3,4,0,1,2,0,1,2,3,4,3,4,1,3,2,4,0,2,1,3,0,4,2,1};
                    for (int i = 0; i < sequenceLength; i++) {
                        stepToKnobMapping[i] = minimalistPattern[i % 32] % primaryKnobs;
                    }
                    break;
                }
                case 2: {
                    int jumpPattern[5] = {0, 2, 4, 1, 3};
                    for (int i = 0; i < sequenceLength; i++) {
                        stepToKnobMapping[i] = jumpPattern[i % 5] % primaryKnobs;
                    }
                    break;
                }
                case 3:
                    for (int i = 0; i < sequenceLength; i++) {
                        stepToKnobMapping[i] = (primaryKnobs - 1 - (i % primaryKnobs));
                    }
                    break;
                case 4: {
                    int revMinimalistPattern[32] = {4,3,2,4,3,2,1,0,1,0,4,3,2,4,3,2,1,0,1,0,3,1,2,0,4,2,3,1,4,0,2,3};
                    for (int i = 0; i < sequenceLength; i++) {
                        stepToKnobMapping[i] = revMinimalistPattern[i % 32] % primaryKnobs;
                    }
                    break;
                }
                case 5: {
                    int revJumpPattern[5] = {3, 1, 4, 2, 0};
                    for (int i = 0; i < sequenceLength; i++) {
                        stepToKnobMapping[i] = revJumpPattern[i % 5] % primaryKnobs;
                    }
                    break;
                }
            }
        }

        float processCVD(float inputCV, float sampleTime, float sampleRate) {
            cvcBuffer[cvcWriteIndex] = inputCV;
            cvcWriteIndex = (cvcWriteIndex + 1) % CH2_CVD_BUFFER_SIZE;
            
            int delaySamples = (int)(cvcDelayTime * sampleRate);
            delaySamples = clamp(delaySamples, 0, CH2_CVD_BUFFER_SIZE - 1);
            
            int readIndex = (cvcWriteIndex - delaySamples + CH2_CVD_BUFFER_SIZE) % CH2_CVD_BUFFER_SIZE;
            float delayedCV = cvcBuffer[readIndex];
            
            return delayedCV * cvcAttenuation;
        }
    };
    CVSequencer ch2Sequencer, ch3Sequencer;

    float globalClockSeconds = 0.5f;
    bool internalClockTriggered = false;
    bool patternClockTriggered = false;
    
    dsp::SchmittTrigger modeTrigger;
    dsp::PulseGenerator gateOutPulse;
    
    int currentStep = 0, sequenceLength = 16, stepToKnobMapping[64];
    float previousVoltage = -999.0f;
    int modeValue = 1;
    int ch2ModeValue = 1;
    int ch3ModeValue = 1;
    int clockSourceValue = 0;

    MADDYPlus() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        
        configParam(FREQ_PARAM, -3.0f, 7.0f, 1.0f, "Frequency", " Hz", 2.0f, 1.0f);
        configParam(SWING_PARAM, 0.0f, 1.0f, 0.0f, "Swing", "°", 0.0f, -90.0f, 180.0f);
        configParam(LENGTH_PARAM, 1.0f, 32.0f, 16.0f, "Length");
        getParamQuantity(LENGTH_PARAM)->snapEnabled = true;
        configParam(DECAY_PARAM, 0.0f, 1.0f, 0.3f, "Decay");
        
        configParam(K1_PARAM, -10.0f, 10.0f, 0.0f, "K1", "V");
        configParam(K2_PARAM, -10.0f, 10.0f, 2.0f, "K2", "V");
        configParam(K3_PARAM, -10.0f, 10.0f, 4.0f, "K3", "V");
        configParam(K4_PARAM, -10.0f, 10.0f, 6.0f, "K4", "V");
        configParam(K5_PARAM, -10.0f, 10.0f, 8.0f, "K5", "V");
        
        configParam(MODE_PARAM, 0.0f, 5.0f, 1.0f, "Mode");
        getParamQuantity(MODE_PARAM)->snapEnabled = true;
        configParam(DENSITY_PARAM, 0.0f, 1.0f, 0.5f, "Density");
        delete paramQuantities[DENSITY_PARAM];
        DensityParamQuantity* densityQuantity = new DensityParamQuantity;
        densityQuantity->module = this;
        densityQuantity->paramId = DENSITY_PARAM;
        densityQuantity->minValue = 0.0f;
        densityQuantity->maxValue = 1.0f;
        densityQuantity->defaultValue = 0.5f;
        densityQuantity->name = "Density";
        paramQuantities[DENSITY_PARAM] = densityQuantity;
        configParam(CHAOS_PARAM, 0.0f, 1.0f, 0.0f, "Chaos", "%", 0.f, 100.f);
        configParam(CLOCK_SOURCE_PARAM, 0.0f, 6.0f, 0.0f, "Clock Source");
        getParamQuantity(CLOCK_SOURCE_PARAM)->snapEnabled = true;
        
        for (int i = 0; i < 3; ++i) {
            configParam(TRACK1_FILL_PARAM + i * 2, 0.0f, 100.0f, 25.0f, string::f("T%d Fill", i+1), "%");
            configParam(TRACK1_DIVMULT_PARAM + i * 2, -3.0f, 3.0f, 0.0f, string::f("T%d Div/Mult", i+1));
            getParamQuantity(TRACK1_DIVMULT_PARAM + i * 2)->snapEnabled = true;
            delete paramQuantities[TRACK1_DIVMULT_PARAM + i * 2];
            paramQuantities[TRACK1_DIVMULT_PARAM + i * 2] = new DivMultParamQuantity;
            paramQuantities[TRACK1_DIVMULT_PARAM + i * 2]->module = this;
            paramQuantities[TRACK1_DIVMULT_PARAM + i * 2]->paramId = TRACK1_DIVMULT_PARAM + i * 2;
            paramQuantities[TRACK1_DIVMULT_PARAM + i * 2]->minValue = -3.0f;
            paramQuantities[TRACK1_DIVMULT_PARAM + i * 2]->maxValue = 3.0f;
            paramQuantities[TRACK1_DIVMULT_PARAM + i * 2]->defaultValue = 0.0f;
            paramQuantities[TRACK1_DIVMULT_PARAM + i * 2]->name = string::f("T%d Div/Mult", i+1);
            paramQuantities[TRACK1_DIVMULT_PARAM + i * 2]->snapEnabled = true;
            
            configOutput(TRACK1_OUTPUT + i, string::f("T%d Trigger", i+1));
        }

        configParam(CH2_CLOCK_SOURCE_PARAM, 0.0f, 6.0f, 0.0f, "CH2 Clock Source");
        getParamQuantity(CH2_CLOCK_SOURCE_PARAM)->snapEnabled = true;
        configParam(CH2_MODE_PARAM, 0.0f, 5.0f, 1.0f, "CH2 Mode");
        getParamQuantity(CH2_MODE_PARAM)->snapEnabled = true;
        configParam(CH2_DENSITY_PARAM, 0.0f, 1.0f, 0.5f, "CH2 Density");
        configParam(CH2_CVD_ATTEN_PARAM, 0.0f, 1.0f, 1.0f, "CH2 CVD Attenuation");
        configParam(CH2_STEP_DELAY_PARAM, 0.0f, 8.0f, 0.0f, "CH2 Step Delay");
        getParamQuantity(CH2_STEP_DELAY_PARAM)->snapEnabled = true;

        configParam(CH3_CLOCK_SOURCE_PARAM, 0.0f, 6.0f, 0.0f, "CH3 Clock Source");
        getParamQuantity(CH3_CLOCK_SOURCE_PARAM)->snapEnabled = true;
        configParam(CH3_MODE_PARAM, 0.0f, 5.0f, 1.0f, "CH3 Mode");
        getParamQuantity(CH3_MODE_PARAM)->snapEnabled = true;
        configParam(CH3_DENSITY_PARAM, 0.0f, 1.0f, 0.5f, "CH3 Density");
        configParam(CH3_CVD_ATTEN_PARAM, 0.0f, 1.0f, 1.0f, "CH3 CVD Attenuation");
        configParam(CH3_STEP_DELAY_PARAM, 0.0f, 8.0f, 0.0f, "CH3 Step Delay");
        getParamQuantity(CH3_STEP_DELAY_PARAM)->snapEnabled = true;
        
        configParam(MANUAL_RESET_PARAM, 0.0f, 1.0f, 0.0f, "Manual Reset");
        
        configInput(CH2_CV_INPUT, "CH2 CV");
        configInput(CH3_CV_INPUT, "CH3 CV");
        
        configOutput(RESET_OUTPUT, "Reset");
        configOutput(CLK_OUTPUT, "Clock");
        configOutput(CHAIN_12_OUTPUT, "Chain 1+2");
        configOutput(CHAIN_23_OUTPUT, "Chain 2+3");
        configOutput(CHAIN_123_OUTPUT, "Chain 1+2+3");
        configOutput(CV_OUTPUT, "CV");
        configOutput(TRIG_OUTPUT, "Trigger");
        configOutput(CH2_CV_OUTPUT, "CH2 CV");
        configOutput(CH2_TRIG_OUTPUT, "CH2 Trigger");
        configOutput(CH3_CV_OUTPUT, "CH3 CV");
        configOutput(CH3_TRIG_OUTPUT, "CH3 Trigger");
        
        configLight(MODE_LIGHT_RED, "Mode Red");
        configLight(MODE_LIGHT_GREEN, "Mode Green");
        configLight(MODE_LIGHT_BLUE, "Mode Blue");
        configLight(MANUAL_RESET_LIGHT, "Manual Reset Light");

        int chain12_indices[] = {0, 1};
        chain12.setTrackIndices(chain12_indices, 2);
        int chain23_indices[] = {1, 2};
        chain23.setTrackIndices(chain23_indices, 2);
        int chain123_indices[] = {0, 1, 0, 2};
        chain123.setTrackIndices(chain123_indices, 4);
        
        ch2Sequencer.reset();
        ch3Sequencer.reset();
        generateMapping();
    }

    void generateMapping() {
        float density = params[DENSITY_PARAM].getValue();
        float chaos = params[CHAOS_PARAM].getValue();
        
        if (density < 0.2f) {
            sequenceLength = 8 + (int)(density * 20);
        } else if (density < 0.4f) {
            sequenceLength = 12 + (int)((density - 0.2f) * 40);
        } else if (density < 0.6f) {
            sequenceLength = 20 + (int)((density - 0.4f) * 40);
        } else {
            sequenceLength = 28 + (int)((density - 0.6f) * 50.1f);
        }
        sequenceLength = clamp(sequenceLength, 8, 48);
        
        if (chaos > 0.0f) {
            float chaosRange = chaos * sequenceLength * 0.5f;
            float randomOffset = (random::uniform() - 0.5f) * 2.0f * chaosRange;
            sequenceLength += (int)randomOffset;
            sequenceLength = clamp(sequenceLength, 4, 64);
        }
        
        int primaryKnobs = (density < 0.2f) ? 2 : (density < 0.4f) ? 3 : (density < 0.6f) ? 4 : 5;
        
        for (int i = 0; i < 64; i++) stepToKnobMapping[i] = 0;
        
        switch (modeValue) {
            case 0:
                for (int i = 0; i < sequenceLength; i++) {
                    stepToKnobMapping[i] = i % primaryKnobs;
                }
                break;
            case 1: {
                int minimalistPattern[32] = {0,1,2,0,1,2,3,4,3,4,0,1,2,0,1,2,3,4,3,4,1,3,2,4,0,2,1,3,0,4,2,1};
                for (int i = 0; i < sequenceLength; i++) {
                    stepToKnobMapping[i] = minimalistPattern[i % 32] % primaryKnobs;
                }
                break;
            }
            case 2: {
                int jumpPattern[5] = {0, 2, 4, 1, 3};
                for (int i = 0; i < sequenceLength; i++) {
                    stepToKnobMapping[i] = jumpPattern[i % 5] % primaryKnobs;
                }
                break;
            }
            case 3:
                for (int i = 0; i < sequenceLength; i++) {
                    stepToKnobMapping[i] = (primaryKnobs - 1 - (i % primaryKnobs));
                }
                break;
            case 4: {
                int revMinimalistPattern[32] = {4,3,2,4,3,2,1,0,1,0,4,3,2,4,3,2,1,0,1,0,3,1,2,0,4,2,3,1,4,0,2,3};
                for (int i = 0; i < sequenceLength; i++) {
                    stepToKnobMapping[i] = revMinimalistPattern[i % 32] % primaryKnobs;
                }
                break;
            }
            case 5: {
                int revJumpPattern[5] = {3, 1, 4, 2, 0};
                for (int i = 0; i < sequenceLength; i++) {
                    stepToKnobMapping[i] = revJumpPattern[i % 5] % primaryKnobs;
                }
                break;
            }
        }
        
        if (chaos > 0.3f) {
            int chaosSteps = (int)(chaos * sequenceLength * 0.3f);
            for (int i = 0; i < chaosSteps; i++) {
                int randomStep = random::u32() % sequenceLength;
                stepToKnobMapping[randomStep] = random::u32() % 5;
            }
        }
    }

    void onReset() override {
        phase = 0.0f;
        swingPhase = 0.0f;
        isSwingBeat = false;
        globalClockSeconds = 0.5f;
        for (int i = 0; i < 3; ++i) {
            tracks[i].reset();
        }
        chain12.reset();
        chain23.reset();
        chain123.reset();
        ch2Sequencer.reset();
        ch3Sequencer.reset();
        
        currentStep = 0;
        generateMapping();
        previousVoltage = -999.0f;
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "modeValue", json_integer(modeValue));
        json_object_set_new(rootJ, "ch2ModeValue", json_integer(ch2ModeValue));
        json_object_set_new(rootJ, "ch3ModeValue", json_integer(ch3ModeValue));
        json_object_set_new(rootJ, "clockSourceValue", json_integer(clockSourceValue));
        
        json_t* attackTimesJ = json_array();
        for (int i = 0; i < 3; ++i) {
            json_array_append_new(attackTimesJ, json_real(tracks[i].attackTime));
        }
        json_object_set_new(rootJ, "attackTimes", attackTimesJ);

        json_t* shiftsJ = json_array();
        for (int i = 0; i < 3; ++i) {
            json_array_append_new(shiftsJ, json_integer(tracks[i].shift));
        }
        json_object_set_new(rootJ, "shifts", shiftsJ);
        
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* modeJ = json_object_get(rootJ, "modeValue");
        if (modeJ) {
            modeValue = json_integer_value(modeJ);
            params[MODE_PARAM].setValue((float)modeValue);
        }

        json_t* ch2ModeJ = json_object_get(rootJ, "ch2ModeValue");
        if (ch2ModeJ) {
            ch2ModeValue = json_integer_value(ch2ModeJ);
            params[CH2_MODE_PARAM].setValue((float)ch2ModeValue);
        }

        json_t* ch3ModeJ = json_object_get(rootJ, "ch3ModeValue");
        if (ch3ModeJ) {
            ch3ModeValue = json_integer_value(ch3ModeJ);
            params[CH3_MODE_PARAM].setValue((float)ch3ModeValue);
        }
    
        json_t* clockSourceJ = json_object_get(rootJ, "clockSourceValue");
        if (clockSourceJ) {
            clockSourceValue = json_integer_value(clockSourceJ);
            params[CLOCK_SOURCE_PARAM].setValue((float)clockSourceValue);
        }
    
        json_t* attackTimesJ = json_object_get(rootJ, "attackTimes");
        if (attackTimesJ) {
            for (int i = 0; i < 3; ++i) {
                json_t* attackTimeJ = json_array_get(attackTimesJ, i);
                if (attackTimeJ) {
                    tracks[i].attackTime = json_real_value(attackTimeJ);
                }
            }
        }
    
        json_t* shiftsJ = json_object_get(rootJ, "shifts");
        if (shiftsJ) {
            for (int i = 0; i < 3; ++i) {
                json_t* shiftJ = json_array_get(shiftsJ, i);
                if (shiftJ) {
                    tracks[i].shift = json_integer_value(shiftJ);
                }
            }
        }
    }

    void process(const ProcessArgs& args) override {
        float freqParam = params[FREQ_PARAM].getValue();
        float freq = std::pow(2.0f, freqParam) * 1.0f;
        
        float swingParam = params[SWING_PARAM].getValue();
        float swing = clamp(swingParam, 0.0f, 1.0f);
        
        static float resetPulseTimer = 0.0f;

        if (params[MANUAL_RESET_PARAM].getValue() > 0.5f) {
            onReset();
            params[MANUAL_RESET_PARAM].setValue(0.0f);
            resetPulseTimer = 0.1f;
            outputs[RESET_OUTPUT].setVoltage(10.0f);
        } else {
            outputs[RESET_OUTPUT].setVoltage(0.0f);
        }

        if (resetPulseTimer > 0.0f) {
            lights[MANUAL_RESET_LIGHT].setBrightness(1.0f);
            resetPulseTimer -= args.sampleTime;
        } else {
            lights[MANUAL_RESET_LIGHT].setBrightness(0.0f);
        }
        
        float deltaPhase = freq * args.sampleTime;
        phase += deltaPhase;
        internalClockTriggered = false;
        
        float phaseThreshold = 1.0f;
        if (isSwingBeat && swing > 0.0f) {
            float swingOffset = swing * 0.25f;
            phaseThreshold = 1.0f + swingOffset;
        }
        
        if (phase >= phaseThreshold) {
            phase -= phaseThreshold;
            clockPulse.trigger(0.001f);
            internalClockTriggered = true;
            globalClockSeconds = phaseThreshold / freq;
            isSwingBeat = !isSwingBeat;
        }
        
        float clockOutput = clockPulse.process(args.sampleTime) ? 10.0f : 0.0f;
        outputs[CLK_OUTPUT].setVoltage(clockOutput);

        int globalLength = (int)std::round(params[LENGTH_PARAM].getValue());
        globalLength = clamp(globalLength, 1, 32);
        
        float decayParam = params[DECAY_PARAM].getValue();

        for (int i = 0; i < 3; ++i) {
            TrackState& track = tracks[i];
            
            int divMultParam = (int)std::round(params[TRACK1_DIVMULT_PARAM + i * 2].getValue());
            track.updateDivMult(divMultParam);

            track.length = globalLength;

            float fillParam = params[TRACK1_FILL_PARAM + i * 2].getValue();
            float fillPercentage = clamp(fillParam, 0.0f, 100.0f);
            track.fill = (int)std::round((fillPercentage / 100.0f) * track.length);

            generateMADDYPlusEuclideanRhythm(track.pattern, track.length, track.fill, track.shift);

            bool trackClockTrigger = track.processClockDivMult(internalClockTriggered, globalClockSeconds, args.sampleTime);

            if (trackClockTrigger) {
                track.stepTrack();
            }
            
            float envelopeOutput = track.processEnvelope(args.sampleTime, decayParam);
            outputs[TRACK1_OUTPUT + i].setVoltage(envelopeOutput);
        }
        
        bool chain12Trigger, chain23Trigger, chain123Trigger;
        float chain12Output = chain12.processStep(tracks, args.sampleTime, internalClockTriggered, decayParam, chain12Trigger);
        outputs[CHAIN_12_OUTPUT].setVoltage(chain12Output);
        
        float chain23Output = chain23.processStep(tracks, args.sampleTime, internalClockTriggered, decayParam, chain23Trigger);
        outputs[CHAIN_23_OUTPUT].setVoltage(chain23Output);
        
        float chain123Output = chain123.processStep(tracks, args.sampleTime, internalClockTriggered, decayParam, chain123Trigger);
        outputs[CHAIN_123_OUTPUT].setVoltage(chain123Output);
        
        modeValue = (int)std::round(params[MODE_PARAM].getValue());
        modeValue = clamp(modeValue, 0, 5);
        
        clockSourceValue = (int)std::round(params[CLOCK_SOURCE_PARAM].getValue());

        patternClockTriggered = false;
        switch (clockSourceValue) {
            case 0:
                patternClockTriggered = internalClockTriggered;
                break;
            case 1:
                patternClockTriggered = tracks[0].justTriggered;
                tracks[0].justTriggered = false;
                break;
            case 2:
                patternClockTriggered = tracks[1].justTriggered;
                tracks[1].justTriggered = false;
                break;
            case 3:
                patternClockTriggered = tracks[2].justTriggered;
                tracks[2].justTriggered = false;
                break;
            case 4:
                patternClockTriggered = chain12.clockPulse.process(args.sampleTime) > 0.0f;
                break;
            case 5:
                patternClockTriggered = chain23.clockPulse.process(args.sampleTime) > 0.0f;
                break;
            case 6:
                patternClockTriggered = chain123.clockPulse.process(args.sampleTime) > 0.0f;
                break;
        }
        
        lights[MODE_LIGHT_RED].setBrightness(modeValue == 0 ? 1.0f : 0.0f);
        lights[MODE_LIGHT_GREEN].setBrightness(modeValue == 1 ? 1.0f : 0.0f);
        lights[MODE_LIGHT_BLUE].setBrightness(modeValue == 2 ? 1.0f : 0.0f);
        
        if (patternClockTriggered) {
            currentStep = (currentStep + 1) % sequenceLength;
            generateMapping();
            
            int newActiveKnob = stepToKnobMapping[currentStep];
            float newVoltage = params[K1_PARAM + newActiveKnob].getValue();
            
            if (newVoltage != previousVoltage) gateOutPulse.trigger(0.01f);
            previousVoltage = newVoltage;
        }
        
        int activeKnob = stepToKnobMapping[currentStep];
        outputs[CV_OUTPUT].setVoltage(params[K1_PARAM + activeKnob].getValue());
        outputs[TRIG_OUTPUT].setVoltage(gateOutPulse.process(args.sampleTime) ? 10.0f : 0.0f);

        ch2Sequencer.densityValue = params[CH2_DENSITY_PARAM].getValue();
        ch2ModeValue = (int)std::round(params[CH2_MODE_PARAM].getValue());
        ch2ModeValue = clamp(ch2ModeValue, 0, 5);
        ch2Sequencer.modeValue = ch2ModeValue;
        ch2Sequencer.stepDelayValue = (int)std::round(params[CH2_STEP_DELAY_PARAM].getValue());
        ch2Sequencer.cvcAttenuation = params[CH2_CVD_ATTEN_PARAM].getValue();
        
        int ch2ClockSource = (int)std::round(params[CH2_CLOCK_SOURCE_PARAM].getValue());
        bool ch2ClockTriggered = false;
        switch (ch2ClockSource) {
            case 0: ch2ClockTriggered = internalClockTriggered; break;
            case 1: ch2ClockTriggered = tracks[0].justTriggered; break;
            case 2: ch2ClockTriggered = tracks[1].justTriggered; break;
            case 3: ch2ClockTriggered = tracks[2].justTriggered; break;
            case 4: ch2ClockTriggered = chain12.clockPulse.process(args.sampleTime) > 0.0f; break;
            case 5: ch2ClockTriggered = chain23.clockPulse.process(args.sampleTime) > 0.0f; break;
            case 6: ch2ClockTriggered = chain123.clockPulse.process(args.sampleTime) > 0.0f; break;
        }

        if (ch2ClockTriggered) {
            if (ch2Sequencer.stepDelayCounter >= ch2Sequencer.stepDelayValue) {
                ch2Sequencer.currentStep = (ch2Sequencer.currentStep + 1) % ch2Sequencer.sequenceLength;
                ch2Sequencer.generateMapping();
                
                int ch2ActiveKnob = ch2Sequencer.stepToKnobMapping[ch2Sequencer.currentStep];
                float ch2NewVoltage = params[K1_PARAM + ch2ActiveKnob].getValue();
                
                if (ch2NewVoltage != ch2Sequencer.previousVoltage) {
                    ch2Sequencer.gateOutPulse.trigger(0.01f);
                }
                ch2Sequencer.previousVoltage = ch2NewVoltage;
                ch2Sequencer.stepDelayCounter = 0;
            } else {
                ch2Sequencer.stepDelayCounter++;
            }
        }

        float ch2InputCV = inputs[CH2_CV_INPUT].getVoltage();
        float ch2ProcessedCV = ch2Sequencer.processCVD(ch2InputCV, args.sampleTime, args.sampleRate);
        
        int ch2ActiveKnob = ch2Sequencer.stepToKnobMapping[ch2Sequencer.currentStep];
        float ch2OutputCV = params[K1_PARAM + ch2ActiveKnob].getValue() + ch2ProcessedCV;
        outputs[CH2_CV_OUTPUT].setVoltage(ch2OutputCV);
        outputs[CH2_TRIG_OUTPUT].setVoltage(ch2Sequencer.gateOutPulse.process(args.sampleTime) ? 10.0f : 0.0f);

        ch3Sequencer.densityValue = params[CH3_DENSITY_PARAM].getValue();
        ch3ModeValue = (int)std::round(params[CH3_MODE_PARAM].getValue());
        ch3ModeValue = clamp(ch3ModeValue, 0, 5);
        ch3Sequencer.modeValue = ch3ModeValue;
        ch3Sequencer.stepDelayValue = (int)std::round(params[CH3_STEP_DELAY_PARAM].getValue());
        ch3Sequencer.cvcAttenuation = params[CH3_CVD_ATTEN_PARAM].getValue();
        
        int ch3ClockSource = (int)std::round(params[CH3_CLOCK_SOURCE_PARAM].getValue());
        bool ch3ClockTriggered = false;
        switch (ch3ClockSource) {
            case 0: ch3ClockTriggered = internalClockTriggered; break;
            case 1: ch3ClockTriggered = tracks[0].justTriggered; break;
            case 2: ch3ClockTriggered = tracks[1].justTriggered; break;
            case 3: ch3ClockTriggered = tracks[2].justTriggered; break;
            case 4: ch3ClockTriggered = chain12.clockPulse.process(args.sampleTime) > 0.0f; break;
            case 5: ch3ClockTriggered = chain23.clockPulse.process(args.sampleTime) > 0.0f; break;
            case 6: ch3ClockTriggered = chain123.clockPulse.process(args.sampleTime) > 0.0f; break;
        }

        if (ch3ClockTriggered) {
            if (ch3Sequencer.stepDelayCounter >= ch3Sequencer.stepDelayValue) {
                ch3Sequencer.currentStep = (ch3Sequencer.currentStep + 1) % ch3Sequencer.sequenceLength;
                ch3Sequencer.generateMapping();
                
                int ch3ActiveKnob = ch3Sequencer.stepToKnobMapping[ch3Sequencer.currentStep];
                float ch3NewVoltage = params[K1_PARAM + ch3ActiveKnob].getValue();
                
                if (ch3NewVoltage != ch3Sequencer.previousVoltage) {
                    ch3Sequencer.gateOutPulse.trigger(0.01f);
                }
                ch3Sequencer.previousVoltage = ch3NewVoltage;
                ch3Sequencer.stepDelayCounter = 0;
            } else {
                ch3Sequencer.stepDelayCounter++;
            }
        }

        float ch3InputCV = inputs[CH3_CV_INPUT].getVoltage();
        float ch3ProcessedCV = ch3Sequencer.processCVD(ch3InputCV, args.sampleTime, args.sampleRate);
        
        int ch3ActiveKnob = ch3Sequencer.stepToKnobMapping[ch3Sequencer.currentStep];
        float ch3OutputCV = params[K1_PARAM + ch3ActiveKnob].getValue() + ch3ProcessedCV;
        outputs[CH3_CV_OUTPUT].setVoltage(ch3OutputCV);
        outputs[CH3_TRIG_OUTPUT].setVoltage(ch3Sequencer.gateOutPulse.process(args.sampleTime) ? 10.0f : 0.0f);
    }
};

struct ModeParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        MADDYPlus* module = dynamic_cast<MADDYPlus*>(this->module);
        if (!module) return "Minimalism";
        
        switch (module->modeValue) {
            case 0: return "Sequential";
            case 1: return "Minimalism";
            case 2: return "Jump";
            case 3: return "Rev Sequential";
            case 4: return "Rev Minimalism";
            case 5: return "Rev Jump";
            default: return "Minimalism";
        }
    }
    
    std::string getLabel() override {
        return "Mode";
    }
};

struct Ch2ModeParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        MADDYPlus* module = dynamic_cast<MADDYPlus*>(this->module);
        if (!module) return "Minimalism";
        switch (module->ch2ModeValue) {
            case 0: return "Sequential";
            case 1: return "Minimalism";
            case 2: return "Jump";
            case 3: return "Rev Sequential";
            case 4: return "Rev Minimalism";
            case 5: return "Rev Jump";
            default: return "Minimalism";
        }
    }
    
    std::string getLabel() override {
        return "Ch2 Mode";
    }
};

struct Ch3ModeParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        MADDYPlus* module = dynamic_cast<MADDYPlus*>(this->module);
        if (!module) return "Minimalism";
        switch (module->ch3ModeValue) {
            case 0: return "Sequential";
            case 1: return "Minimalism";
            case 2: return "Jump";
            case 3: return "Rev Sequential";
            case 4: return "Rev Minimalism";
            case 5: return "Rev Jump";
            default: return "Minimalism";
        }
    }
    
    std::string getLabel() override {
        return "Ch3 Mode";
    }
};

struct ClockSourceParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        MADDYPlus* module = dynamic_cast<MADDYPlus*>(this->module);
        if (!module) return "LFO";
        
        switch (module->clockSourceValue) {
            case 0: return "LFO";
            case 1: return "T1";
            case 2: return "T2";
            case 3: return "T3";
            case 4: return "12";
            case 5: return "23";
            case 6: return "1213";
            default: return "LFO";
        }
    }
    
    std::string getLabel() override {
        return "Clock Source";
    }
};

struct MADDYPlusWidget : ModuleWidget {
    MADDYPlusWidget(MADDYPlus* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "MADDYPlus.png")));

        box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        addOutput(createOutputCentered<PJ301MPort>(Vec(60, 52), module, MADDYPlus::RESET_OUTPUT));
        
        addChild(createLightCentered<MediumLight<RedLight>>(Vec(72, 50), module, MADDYPlus::MANUAL_RESET_LIGHT));
        addParam(createParamCentered<VCVButton>(Vec(72, 50), module, MADDYPlus::MANUAL_RESET_PARAM));

        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(98, 52), module, MADDYPlus::FREQ_PARAM));
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(60, 85), module, MADDYPlus::SWING_PARAM));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(98, 85), module, MADDYPlus::CLK_OUTPUT));
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(20, 52), module, MADDYPlus::LENGTH_PARAM));
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(20, 85), module, MADDYPlus::DECAY_PARAM));
        
        float trackY[3] = {107, 183, 259};
        
        for (int i = 0; i < 3; ++i) {
            float y = trackY[i];
            
            addParam(createParamCentered<RoundBlackKnob>(Vec(20, y + 20), module, MADDYPlus::TRACK1_FILL_PARAM + i * 2));
            
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(20, y + 53), module, MADDYPlus::TRACK1_DIVMULT_PARAM + i * 2));
        }
        
        float cvY[5] = {127, 172, 217, 262, 307};
        for (int i = 0; i < 5; ++i) {
            addParam(createParamCentered<RoundBlackKnob>(Vec(60, cvY[i] - 5), module, MADDYPlus::K1_PARAM + i));
        }
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(98, 116), module, MADDYPlus::MODE_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(Vec(98, 154), module, MADDYPlus::DENSITY_PARAM));
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(98, 194), module, MADDYPlus::CHAOS_PARAM));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(98, 234), module, MADDYPlus::CV_OUTPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(98, 274), module, MADDYPlus::TRIG_OUTPUT));
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(98, 308), module, MADDYPlus::CLOCK_SOURCE_PARAM));

        float ch2OffsetX = 8 * 15.0f; // 8 * RACK_GRID_WIDTH = 120
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(ch2OffsetX + 15, 50), module, MADDYPlus::CH2_CLOCK_SOURCE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(ch2OffsetX + 15, 95), module, MADDYPlus::CH2_MODE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(ch2OffsetX + 45, 70), module, MADDYPlus::CH2_DENSITY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(ch2OffsetX + 15, 140), module, MADDYPlus::CH2_CVD_ATTEN_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(ch2OffsetX + 45, 115), module, MADDYPlus::CH2_STEP_DELAY_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(ch2OffsetX + 45, 160), module, MADDYPlus::CH2_CV_INPUT));
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(ch2OffsetX + 15, 197), module, MADDYPlus::CH3_CLOCK_SOURCE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(ch2OffsetX + 15, 242), module, MADDYPlus::CH3_MODE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(ch2OffsetX + 45, 217), module, MADDYPlus::CH3_DENSITY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(ch2OffsetX + 15, 287), module, MADDYPlus::CH3_CVD_ATTEN_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(ch2OffsetX + 45, 267), module, MADDYPlus::CH3_STEP_DELAY_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(ch2OffsetX + 45, 312), module, MADDYPlus::CH3_CV_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(Vec(ch2OffsetX + 15, 343), module, MADDYPlus::CH2_CV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(ch2OffsetX + 15, 368), module, MADDYPlus::CH2_TRIG_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(ch2OffsetX + 45, 343), module, MADDYPlus::CH3_CV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(ch2OffsetX + 45, 368), module, MADDYPlus::CH3_TRIG_OUTPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(24, 343), module, MADDYPlus::TRACK1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(24, 368), module, MADDYPlus::CHAIN_12_OUTPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(64, 343), module, MADDYPlus::TRACK2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(64, 368), module, MADDYPlus::CHAIN_23_OUTPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(102, 343), module, MADDYPlus::TRACK3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(102, 368), module, MADDYPlus::CHAIN_123_OUTPUT));
        
        if (module) {
            delete module->paramQuantities[MADDYPlus::MODE_PARAM];
            ModeParamQuantity* modeQuantity = new ModeParamQuantity;
            modeQuantity->module = module;
            modeQuantity->paramId = MADDYPlus::MODE_PARAM;
            modeQuantity->minValue = 0.0f;
            modeQuantity->maxValue = 5.0f;
            modeQuantity->defaultValue = 1.0f;
            modeQuantity->name = "Mode";
            modeQuantity->snapEnabled = true;
            module->paramQuantities[MADDYPlus::MODE_PARAM] = modeQuantity;

            delete module->paramQuantities[MADDYPlus::CH2_MODE_PARAM];
            Ch2ModeParamQuantity* ch2ModeQuantity = new Ch2ModeParamQuantity;
            ch2ModeQuantity->module = module;
            ch2ModeQuantity->paramId = MADDYPlus::CH2_MODE_PARAM;
            ch2ModeQuantity->minValue = 0.0f;
            ch2ModeQuantity->maxValue = 5.0f;
            ch2ModeQuantity->defaultValue = 1.0f;
            ch2ModeQuantity->name = "Ch2 Mode";
            ch2ModeQuantity->snapEnabled = true;
            module->paramQuantities[MADDYPlus::CH2_MODE_PARAM] = ch2ModeQuantity;

            delete module->paramQuantities[MADDYPlus::CH3_MODE_PARAM];
            Ch3ModeParamQuantity* ch3ModeQuantity = new Ch3ModeParamQuantity;
            ch3ModeQuantity->module = module;
            ch3ModeQuantity->paramId = MADDYPlus::CH3_MODE_PARAM;
            ch3ModeQuantity->minValue = 0.0f;
            ch3ModeQuantity->maxValue = 5.0f;
            ch3ModeQuantity->defaultValue = 1.0f;
            ch3ModeQuantity->name = "Ch3 Mode";
            ch3ModeQuantity->snapEnabled = true;
            module->paramQuantities[MADDYPlus::CH3_MODE_PARAM] = ch3ModeQuantity;

            delete module->paramQuantities[MADDYPlus::CLOCK_SOURCE_PARAM];
            ClockSourceParamQuantity* clockSourceQuantity = new ClockSourceParamQuantity;
            clockSourceQuantity->module = module;
            clockSourceQuantity->paramId = MADDYPlus::CLOCK_SOURCE_PARAM;
            clockSourceQuantity->minValue = 0.0f;
            clockSourceQuantity->maxValue = 6.0f;
            clockSourceQuantity->defaultValue = 0.0f;
            clockSourceQuantity->name = "Clock Source";
            clockSourceQuantity->snapEnabled = true;
            module->paramQuantities[MADDYPlus::CLOCK_SOURCE_PARAM] = clockSourceQuantity;
        }
    }

    void appendContextMenu(Menu* menu) override {
        MADDYPlus* module = getModule<MADDYPlus>();
        if (!module) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Pattern Mode"));
        
        menu->addChild(createCheckMenuItem("Sequential", "",
            [=]() { return module->modeValue == 0; },
            [=]() { 
                module->modeValue = 0;
                module->generateMapping();
            }
        ));
        
        menu->addChild(createCheckMenuItem("Minimalism", "",
            [=]() { return module->modeValue == 1; },
            [=]() { 
                module->modeValue = 1;
                module->generateMapping();
            }
        ));
        
        menu->addChild(createCheckMenuItem("Jump", "",
            [=]() { return module->modeValue == 2; },
            [=]() { 
                module->modeValue = 2;
                module->generateMapping();
            }
        ));

        menu->addChild(createCheckMenuItem("Rev Sequential", "",
            [=]() { return module->modeValue == 3; },
            [=]() { 
                module->modeValue = 3;
                module->generateMapping();
            }
        ));

        menu->addChild(createCheckMenuItem("Rev Minimalism", "",
            [=]() { return module->modeValue == 4; },
            [=]() { 
                module->modeValue = 4;
                module->generateMapping();
            }
        ));

        menu->addChild(createCheckMenuItem("Rev Jump", "",
            [=]() { return module->modeValue == 5; },
            [=]() { 
                module->modeValue = 5;
                module->generateMapping();
            }
        ));
        
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Clock Source"));
        
        std::vector<std::string> clockSourceNames = {"LFO", "T1", "T2", "T3", "12", "23", "1213"};
        
        for (int i = 0; i < 7; ++i) {
            menu->addChild(createCheckMenuItem(clockSourceNames[i], "",
                [=]() { return module->clockSourceValue == i; },
                [=]() { module->clockSourceValue = i; }
            ));
        }
        
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Attack Time"));
        
        std::vector<std::pair<std::string, float>> attackTimeChoices = {
            {"0.5ms", 0.0005f},
            {"1ms", 0.001f},
            {"3ms", 0.003f},
            {"6ms (Default)", 0.006f},
            {"10ms", 0.01f},
            {"15ms", 0.015f},
            {"20ms", 0.02f}
        };
        
        for (const auto& choice : attackTimeChoices) {
            menu->addChild(createMenuItem(choice.first, "",
                [=]() { 
                    for (int i = 0; i < 3; ++i) {
                        module->tracks[i].attackTime = choice.second;
                    }
                }
            ));
        }
        
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Shift Settings"));

        for (int trackId = 0; trackId < 3; trackId++) {
            std::string trackLabel = string::f("Track %d Shift", trackId + 1);
            menu->addChild(createMenuLabel(trackLabel));
            
            struct TrackShiftMenu : MenuItem {
                MADDYPlus* module;
                int trackIndex;
                
                TrackShiftMenu(MADDYPlus* module, int trackIndex) : module(module), trackIndex(trackIndex) {
                    text = string::f("Track %d Shift", trackIndex + 1);
                    rightText = string::f("%d step", module ? module->tracks[trackIndex].shift : 0);
                }
                
                Menu* createChildMenu() override {
                    Menu* menu = new Menu();
                    
                    for (int shift = 0; shift <= 4; shift++) {
                        struct ShiftMenuItem : MenuItem {
                            MADDYPlus* module;
                            int trackIndex;
                            int shiftValue;
                            
                            ShiftMenuItem(MADDYPlus* module, int trackIndex, int shiftValue) 
                                : module(module), trackIndex(trackIndex), shiftValue(shiftValue) {
                                text = string::f("%d step", shiftValue);
                                if (module && module->tracks[trackIndex].shift == shiftValue) {
                                    rightText = CHECKMARK_STRING;
                                }
                            }
                            
                            void onAction(const event::Action& e) override {
                                if (module && trackIndex >= 0 && trackIndex < 3) {
                                    module->tracks[trackIndex].shift = shiftValue;
                                }
                            }
                        };
                        
                        menu->addChild(new ShiftMenuItem(module, trackIndex, shift));
                    }
                    
                    return menu;
                }
            };
            
            menu->addChild(new TrackShiftMenu(module, trackId));
        }
    }
}; 

Model* modelMADDYPlus = createModel<MADDYPlus, MADDYPlusWidget>("MADDYPlus");