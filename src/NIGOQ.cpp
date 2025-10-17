#include "plugin.hpp"
#include "ChowDSP.hpp"
#include <cmath>

struct NIGOQ : Module {
    enum ParamIds {
        MOD_FREQ,
        FINAL_FREQ,
        LPF_CUTOFF,
        ORDER,
        HARMONICS,
        MOD_WAVE,
        FM_AMT_ATTEN,
        FOLD_AMT_ATTEN,
        AM_AMT_ATTEN,
        MOD_FM_ATTEN,
        FINAL_FM_ATTEN,
        DECAY,
        BASS,
        FM_AMT,
        FOLD_AMT,
        AM_AMT,
        SYNC_MODE,
        SCOPE_TIME,
        TRIG_PARAM,
        ATTACK_TIME,
        NUM_PARAMS
    };
    enum InputIds {
        TRIG_IN,
        MOD_WAVE_CV,
        MOD_EXT_IN,
        FINAL_EXT_IN,
        LPF_CUTOFF_CV,
        ORDER_CV,
        FM_AMT_CV,
        HARMONICS_CV,
        FOLD_AMT_CV,
        AM_AMT_CV,
        MOD_FM_IN,
        MOD_1VOCT,
        FINAL_FM_IN,
        FINAL_1VOCT,
        NUM_INPUTS
    };
    enum OutputIds {
        MOD_SIGNAL_OUT,
        FINAL_SINE_OUT,
        FINAL_FINAL_OUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        TRIG_LIGHT,
        NUM_LIGHTS
    };

    // Scope display (like Observer)
    struct ScopePoint {
        float min = INFINITY;
        float max = -INFINITY;
    };

    static constexpr int SCOPE_BUFFER_SIZE = 256;
    ScopePoint finalBuffer[SCOPE_BUFFER_SIZE];
    ScopePoint modBuffer[SCOPE_BUFFER_SIZE];
    ScopePoint currentFinal;
    ScopePoint currentMod;
    int bufferIndex = 0;
    int frameIndex = 0;

    dsp::SchmittTrigger scopeTriggers[16];

    // Core oscillator state
    float modPhase = 0.f;
    float finalPhase = 0.f;
    float prevFinalPhase = 0.f;  // For sync detection

    // AD Envelope implementation
    enum EnvelopePhase {
        ENV_IDLE,
        ENV_ATTACK,
        ENV_DECAY
    };

    struct ADEnvelope {
        EnvelopePhase phase = ENV_IDLE;
        float phaseTime = 0.0f;
        float output = 0.0f;
        dsp::SchmittTrigger trigger;

        void reset() {
            phase = ENV_IDLE;
            phaseTime = 0.0f;
            output = 0.0f;
            trigger.reset();
        }

        // Apply curve function
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

        float process(float sampleTime, float triggerVoltage, float attackTime, float decayTime, float curveParam = 0.5f) {
            // Trigger detection with retrigger capability
            if (trigger.process(triggerVoltage)) {
                phase = ENV_ATTACK;
                phaseTime = 0.0f;
            }

            switch (phase) {
                case ENV_IDLE:
                    output = 0.0f;
                    break;

                case ENV_ATTACK:
                    phaseTime += sampleTime;
                    if (phaseTime >= attackTime) {
                        phase = ENV_DECAY;
                        phaseTime = 0.0f;
                        output = 1.0f;
                    } else {
                        float t = phaseTime / attackTime;
                        output = applyCurve(t, curveParam);
                    }
                    break;

                case ENV_DECAY:
                    phaseTime += sampleTime;
                    if (decayTime <= 0.0f) {
                        output = 0.0f;
                        phase = ENV_IDLE;
                        phaseTime = 0.0f;
                    } else if (phaseTime >= decayTime) {
                        output = 0.0f;
                        phase = ENV_IDLE;
                        phaseTime = 0.0f;
                    } else {
                        float t = phaseTime / decayTime;
                        output = 1.0f - applyCurve(t, curveParam);
                    }
                    break;
            }

            return clamp(output, 0.0f, 1.0f);
        }
    };

    ADEnvelope modEnvelope;
    ADEnvelope finalEnvelope;

    float attackTime = 0.01f;  // 10ms attack

    // DC blocking for rectifier function
    float orderDCBlock = 0.0f;

    // Simple one-pole lowpass filter
    struct SimpleLP {
        float z1 = 0.0f;
        float cutoff = 1.0f;
        float sampleRate = 44100.0f;

        void setSampleRate(float sr) {
            sampleRate = sr;
        }

        void setCutoff(float cutoffFreq) {
            float fc = cutoffFreq / sampleRate;
            fc = clamp(fc, 0.0001f, 0.4999f);
            float wc = std::tan(M_PI * fc);
            cutoff = wc / (1.0f + wc);
        }

        float process(float input) {
            z1 = input * cutoff + z1 * (1.0f - cutoff);
            return z1;
        }

        void reset() {
            z1 = 0.0f;
        }
    };

    // Two-pole lowpass filter (12dB/oct)
    struct TwoPoleLP {
        SimpleLP lp1, lp2;
        float resonance = 0.0f;

        void setSampleRate(float sr) {
            lp1.setSampleRate(sr);
            lp2.setSampleRate(sr);
        }

        void setCutoff(float cutoffFreq) {
            lp1.setCutoff(cutoffFreq);
            lp2.setCutoff(cutoffFreq);
        }

        float process(float input) {
            float feedback = lp2.z1 * resonance * 0.4f;
            float stage1 = lp1.process(input - feedback);
            float output = lp2.process(stage1);
            return output;
        }

        void reset() {
            lp1.reset();
            lp2.reset();
        }
    };

    TwoPoleLP lpFilter;

    // Parameter smoothing
    struct SmoothedParam {
        float value = 0.f;
        float target = 0.f;

        void setTarget(float newTarget) {
            target = newTarget;
        }

        float process() {
            const float alpha = 0.995f;
            value = value * alpha + target * (1.f - alpha);
            return value;
        }

        void reset(float initValue) {
            value = initValue;
            target = initValue;
        }
    };

    SmoothedParam smoothedModFreq;
    SmoothedParam smoothedFinalFreq;
    SmoothedParam smoothedLpfCutoff;
    SmoothedParam smoothedOrder;
    SmoothedParam smoothedHarmonics;
    SmoothedParam smoothedWaveMorph;
    SmoothedParam smoothedFmAmt;
    SmoothedParam smoothedFoldAmt;
    SmoothedParam smoothedSymAmt;
    SmoothedParam smoothedBass;

    // Oversampling (ChowDSP, like ChoppingKinky)
    chowdsp::VariableOversampling<6> oversampler;  // 12th order Butterworth
    int oversamplingIndex = 2;  // default 4x oversampling (2^2 = 4)

    // Wavefolding function with smooth, rounded folds
    float wavefold(float input, float amount) {
        if (amount <= 0.0f) return input;

        float gain = 1.0f + amount * 11.0f;
        float amplified = input * gain;

        float folded = std::cos(amplified * M_PI * 0.25f);

        if (amount > 0.35f) {
            float fold2 = std::cos(amplified * M_PI * 0.5f);
            float blend = (amount - 0.35f) / 0.65f;
            blend = blend * blend;
            folded = folded * (1.0f - blend * 0.3f) + fold2 * blend * 0.3f;
        }

        if (amount > 0.6f) {
            float fold3 = std::cos(amplified * M_PI * 0.75f);
            float blend = (amount - 0.6f) / 0.4f;
            blend = blend * blend;
            folded = folded * (1.0f - blend * 0.2f) + fold3 * blend * 0.2f;
        }

        if (amount > 0.8f) {
            float fold4 = std::cos(amplified * M_PI);
            float blend = (amount - 0.8f) / 0.2f;
            blend = blend * blend;
            folded = folded * (1.0f - blend * 0.1f) + fold4 * blend * 0.1f;
        }

        float output = std::tanh(folded);
        output = std::tanh(output * 1.5f);

        float wetness = amount * amount;
        return input * (1.0f - wetness * 0.8f) + output * (wetness * 0.8f + 0.2f);
    }

    // Asymmetric rectifier function
    float asymmetricRectifier(float input, float amount) {
        float output = input;

        if (input < 0.0f) {
            output = input * (1.0f - amount);
        }

        // DC blocking
        float dcBlockCutoff = 0.995f - amount * 0.01f;
        orderDCBlock = orderDCBlock * dcBlockCutoff + output * (1.0f - dcBlockCutoff);
        output = output - orderDCBlock;

        // Normalize output level
        float compensation = 1.0f + amount * 0.5f;
        output *= compensation;

        // Soft clipping
        output = std::tanh(output * 0.8f) * 1.25f;

        return output;
    }

    // PolyBLEP function for anti-aliasing
    float polyBLEP(float t, float dt) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        else if (t > 1.0f - dt) {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    // Generate morphing waveforms with PolyBLEP anti-aliasing
    float generateMorphingWave(float phase, float morphParam, float phaseInc = 0.01f) {
        float output = 0.f;

        if (morphParam <= 0.2f) {
            // Morph between sine and triangle
            float blend = morphParam * 5.f;
            float sine = std::sin(2.f * M_PI * phase);
            float triangle = 2.f * std::abs(2.f * (phase - std::floor(phase + 0.5f))) - 1.f;
            output = sine * (1.f - blend) + triangle * blend;
        }
        else if (morphParam <= 0.4f) {
            // Morph between triangle and saw
            float blend = (morphParam - 0.2f) * 5.f;
            float triangle = 2.f * std::abs(2.f * (phase - std::floor(phase + 0.5f))) - 1.f;

            // Band-limited saw with PolyBLEP
            float saw = 1.f - 2.f * phase;
            saw += polyBLEP(phase, phaseInc);

            output = triangle * (1.f - blend) + saw * blend;
        }
        else if (morphParam <= 0.6f) {
            // Morph between saw and pulse
            float blend = (morphParam - 0.4f) * 5.f;

            // Band-limited saw
            float saw = 1.f - 2.f * phase;
            saw += polyBLEP(phase, phaseInc);

            // Band-limited pulse (98% duty)
            float pulseWidth = 0.98f;
            float pulse = phase < pulseWidth ? 1.f : -1.f;
            pulse += polyBLEP(phase, phaseInc);
            pulse -= polyBLEP(std::fmod(phase + (1.f - pulseWidth), 1.f), phaseInc);

            output = saw * (1.f - blend) + pulse * blend;
        }
        else {
            // Variable pulse width
            float pwParam = (morphParam - 0.6f) / 0.4f;
            float pulseWidth = 0.98f - pwParam * 0.97f;

            // Band-limited pulse
            float pulse = phase < pulseWidth ? 1.f : -1.f;
            pulse += polyBLEP(phase, phaseInc);
            pulse -= polyBLEP(std::fmod(phase + (1.f - pulseWidth), 1.f), phaseInc);

            output = pulse;
        }

        return output;
    }

    NIGOQ() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        // Configure parameters with exponential display
        // MOD_FREQ: 0.001 Hz to 6000 Hz (6 kHz)
        configParam(MOD_FREQ, 0.f, 1.f, 0.25f, "Modulation Frequency", " Hz", std::pow(6000.0f / 0.001f, 1.f), 0.001f);
        // FINAL_FREQ: 20 Hz to 8000 Hz (8 kHz)
        configParam(FINAL_FREQ, 0.f, 1.f, 0.3f, "Final Frequency", " Hz", std::pow(8000.0f / 20.0f, 1.f), 20.0f);
        // LPF_CUTOFF: 10 Hz to 20000 Hz (20 kHz)
        configParam(LPF_CUTOFF, 0.f, 1.f, 0.7504f, "LPF Cutoff", " Hz", std::pow(20000.0f / 10.0f, 1.f), 10.0f);
        configParam(ORDER, 0.f, 1.f, 0.15f, "Rectify Amount", "%", 0.f, 100.f);
        configParam(HARMONICS, 0.f, 1.f, 0.25f, "Wavefolding", "%", 0.f, 100.f);
        configParam(MOD_WAVE, 0.f, 1.f, 0.15f, "Modulation Wave Shape");
        configParam(FM_AMT_ATTEN, 0.f, 1.f, 0.7f, "FM CV Attenuator", "%", 0.f, 100.f);
        configParam(FOLD_AMT_ATTEN, 0.f, 1.f, 0.7f, "TM CV Attenuator", "%", 0.f, 100.f);
        configParam(AM_AMT_ATTEN, 0.f, 1.f, 0.7f, "RECT CV Attenuator", "%", 0.f, 100.f);
        configParam(MOD_FM_ATTEN, 0.f, 1.f, 0.f, "Mod FM Attenuator", "%", 0.f, 100.f);
        configParam(FINAL_FM_ATTEN, 0.f, 1.f, 0.f, "Final FM Attenuator", "%", 0.f, 100.f);
        configParam(DECAY, 0.f, 1.f, 0.73f, "Decay Time", " s");
        configParam(BASS, 0.f, 1.f, 0.3f, "Bass/Sine Mix", "%", 0.f, 100.f);
        configParam(FM_AMT, 0.f, 1.f, 0.05f, "Linear FM Index");
        configParam(FOLD_AMT, 0.f, 1.f, 0.5f, "TM Amount", "%", 0.f, 100.f);
        configParam(AM_AMT, 0.f, 1.f, 0.2f, "RECT Mod Amount", "%", 0.f, 100.f);
        configParam(SYNC_MODE, 0.f, 2.f, 0.f, "Sync Mode");
        // Scope time parameter (exactly like Observer)
        const float maxTime = -std::log2(5e1f);
        const float minTime = -std::log2(5e-3f);
        const float defaultTime = -std::log2(5e-1f);
        configParam(SCOPE_TIME, maxTime, minTime, defaultTime, "Time", " ms/screen", 1 / 2.f, 1000);
        configSwitch(TRIG_PARAM, 0.f, 1.f, 1.f, "Trigger", {"Enabled", "Disabled"});
        // Attack time: 0.1ms - 100ms, default 10ms (logarithmic scale)
        configParam(ATTACK_TIME, 0.f, 1.f, 0.5f, "Attack Time", " ms", std::pow(100.0f / 0.1f, 1.f), 0.1f);

        configInput(TRIG_IN, "Trigger");
        configInput(MOD_WAVE_CV, "Modulation Wave CV");
        configInput(MOD_EXT_IN, "External Modulation Input");
        configInput(FINAL_EXT_IN, "External Final Input");
        configInput(LPF_CUTOFF_CV, "LPF Cutoff CV");
        configInput(ORDER_CV, "Rectify CV");
        configInput(FM_AMT_CV, "FM Amount CV");
        configInput(HARMONICS_CV, "Harmonics CV");
        configInput(FOLD_AMT_CV, "Fold Amount CV");
        configInput(AM_AMT_CV, "RECT Mod Amount CV");
        configInput(MOD_FM_IN, "Modulation FM");
        configInput(MOD_1VOCT, "Modulation 1V/Oct");
        configInput(FINAL_FM_IN, "Final FM");
        configInput(FINAL_1VOCT, "Final 1V/Oct");

        configOutput(MOD_SIGNAL_OUT, "Modulation Signal");
        configOutput(FINAL_SINE_OUT, "Final Sine");
        configOutput(FINAL_FINAL_OUT, "Final Output");

        configLight(TRIG_LIGHT, "Trigger");

        // Initialize smoothed parameters
        smoothedModFreq.reset(params[MOD_FREQ].getValue());
        smoothedFinalFreq.reset(params[FINAL_FREQ].getValue());
        smoothedLpfCutoff.reset(params[LPF_CUTOFF].getValue());
        smoothedOrder.reset(params[ORDER].getValue());
        smoothedHarmonics.reset(params[HARMONICS].getValue());
        smoothedWaveMorph.reset(params[MOD_WAVE].getValue());
        smoothedFmAmt.reset(params[FM_AMT].getValue());
        smoothedFoldAmt.reset(params[FOLD_AMT].getValue());
        smoothedSymAmt.reset(params[AM_AMT].getValue());
        smoothedBass.reset(params[BASS].getValue());

        lpFilter.setSampleRate(APP->engine->getSampleRate());
        lpFilter.setCutoff(8000.0f);
        lpFilter.reset();

        // Initialize oversampler
        oversampler.setOversamplingIndex(oversamplingIndex);
        oversampler.reset(APP->engine->getSampleRate());
    }

    void onSampleRateChange() override {
        lpFilter.setSampleRate(APP->engine->getSampleRate());
        oversampler.reset(APP->engine->getSampleRate());
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "oversamplingIndex", json_integer(oversamplingIndex));
        json_object_set_new(rootJ, "attackTime", json_real(attackTime));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* oversamplingIndexJ = json_object_get(rootJ, "oversamplingIndex");
        if (oversamplingIndexJ) {
            oversamplingIndex = json_integer_value(oversamplingIndexJ);
            oversampler.setOversamplingIndex(oversamplingIndex);
            oversampler.reset(APP->engine->getSampleRate());
        }

        json_t* attackTimeJ = json_object_get(rootJ, "attackTime");
        if (attackTimeJ) {
            attackTime = json_real_value(attackTimeJ);
        }
    }

    void process(const ProcessArgs& args) override {
        // Update smoothed parameter targets
        smoothedModFreq.setTarget(params[MOD_FREQ].getValue());
        smoothedFinalFreq.setTarget(params[FINAL_FREQ].getValue());
        smoothedLpfCutoff.setTarget(params[LPF_CUTOFF].getValue());
        smoothedOrder.setTarget(params[ORDER].getValue());
        smoothedHarmonics.setTarget(params[HARMONICS].getValue());
        smoothedWaveMorph.setTarget(params[MOD_WAVE].getValue());
        smoothedFmAmt.setTarget(params[FM_AMT].getValue());
        smoothedFoldAmt.setTarget(params[FOLD_AMT].getValue());
        smoothedSymAmt.setTarget(params[AM_AMT].getValue());
        smoothedBass.setTarget(params[BASS].getValue());

        // Process smoothed parameters
        float modFreqKnob = smoothedModFreq.process();
        const float kModFreqKnobMin = 0.001f;
        const float kModFreqKnobMax = 6000.0f;
        float modFreq = kModFreqKnobMin * std::pow(kModFreqKnobMax / kModFreqKnobMin, modFreqKnob);

        // Apply 1V/Oct CV
        if (inputs[MOD_1VOCT].isConnected()) {
            float voct = inputs[MOD_1VOCT].getVoltage();
            modFreq *= std::pow(2.f, voct);
        }

        // Apply FM
        if (inputs[MOD_FM_IN].isConnected()) {
            float fmAmount = params[MOD_FM_ATTEN].getValue();
            float fmSignal = inputs[MOD_FM_IN].getVoltage() / 5.f;
            modFreq *= (1.f + fmSignal * fmAmount);
        }

        modFreq = clamp(modFreq, 0.001f, args.sampleRate / 2.f);

        // Get wave morph parameter
        float waveMorph = smoothedWaveMorph.process();
        if (inputs[MOD_WAVE_CV].isConnected()) {
            float waveCV = inputs[MOD_WAVE_CV].getVoltage() / 10.f;
            waveMorph = clamp(waveMorph + waveCV, 0.f, 1.f);
        }

        // Get decay parameter
        float decayParam = params[DECAY].getValue();
        float decayTime;
        if (decayParam <= 0.5f) {
            decayTime = decayParam * 0.6f;
        } else {
            decayTime = 0.3f + (decayParam - 0.5f) * 5.4f;
        }

        // If no trigger input connected, always produce sound (drone mode)
        float triggerVoltage = inputs[TRIG_IN].isConnected() ? inputs[TRIG_IN].getVoltage() : 10.0f;
        bool isLongDecay = (decayTime >= 3.f) || !inputs[TRIG_IN].isConnected();

        if (isLongDecay) {
            modEnvelope.reset();
            finalEnvelope.reset();
        }

        // Get attack time from parameter (0.1ms - 100ms)
        float attackTimeKnob = params[ATTACK_TIME].getValue();
        const float kAttackTimeMin = 0.1f / 1000.f;  // 0.1ms in seconds
        const float kAttackTimeMax = 100.f / 1000.f;  // 100ms in seconds
        attackTime = kAttackTimeMin * std::pow(kAttackTimeMax / kAttackTimeMin, attackTimeKnob);

        // Calculate VCA gains
        float modVcaGain, finalVcaGain;
        if (isLongDecay) {
            modVcaGain = 1.f;
            finalVcaGain = 1.f;
        } else {
            const float fixedCurve = -0.95f;
            modVcaGain = modEnvelope.process(args.sampleTime, triggerVoltage, attackTime, decayTime, fixedCurve);
            finalVcaGain = finalEnvelope.process(args.sampleTime, triggerVoltage, attackTime, decayTime, fixedCurve);
        }

        // Generate MOD signal
        float modOutput = 0.f;
        float modSignal = 0.f;

        if (inputs[MOD_EXT_IN].isConnected()) {
            modSignal = inputs[MOD_EXT_IN].getVoltage() / 5.f;
            modSignal = clamp(modSignal, -1.f, 1.f);
            modOutput = (modSignal + 1.f) * 5.f;
        } else {
            float deltaPhase = modFreq * args.sampleTime;
            modPhase += deltaPhase;
            if (modPhase >= 1.f) {
                modPhase -= 1.f;
            }

            modSignal = generateMorphingWave(modPhase, waveMorph, deltaPhase);
            modOutput = (modSignal + 1.f) * 5.f;
        }

        // Apply VCA to MOD
        float modOutputWithVca = modOutput * modVcaGain;
        float modSignalForModulation;
        if (inputs[MOD_EXT_IN].isConnected()) {
            modSignalForModulation = modSignal * modVcaGain;
        } else {
            modSignalForModulation = (modOutputWithVca - 5.f) / 5.f;
        }

        // Get FINAL frequency
        float finalFreqKnob = smoothedFinalFreq.process();
        const float kFinalFreqKnobMin = 20.0f;
        const float kFinalFreqKnobMax = 8000.0f;
        float finalFreq = kFinalFreqKnobMin * std::pow(kFinalFreqKnobMax / kFinalFreqKnobMin, finalFreqKnob);

        // Apply 1V/Oct CV
        if (inputs[FINAL_1VOCT].isConnected()) {
            float voct = inputs[FINAL_1VOCT].getVoltage();
            finalFreq *= std::pow(2.f, voct);
        }

        // Apply external Linear FM
        if (inputs[FINAL_FM_IN].isConnected()) {
            float fmAmount = params[FINAL_FM_ATTEN].getValue();
            float fmSignal = inputs[FINAL_FM_IN].getVoltage() / 5.f;
            finalFreq *= (1.f + fmSignal * fmAmount * 10.f);
        }

        // Internal FM amount
        float fmModAmount = smoothedFmAmt.process();
        if (inputs[FM_AMT_CV].isConnected()) {
            float fmAttenuation = params[FM_AMT_ATTEN].getValue();
            float fmCV = inputs[FM_AMT_CV].getVoltage() / 10.f;
            fmModAmount += fmCV * fmAttenuation;
            fmModAmount = clamp(fmModAmount, 0.f, 1.f);
        }

        // Track previous phase for sync
        prevFinalPhase = finalPhase;

        // Calculate base phase increment
        float basePhaseInc = finalFreq * args.sampleTime;

        // Calculate FM phase increment
        float fmPhaseInc = 0.0f;
        if (fmModAmount > 0.0f) {
            float fmIndex = fmModAmount * fmModAmount * 4.f;
            fmPhaseInc = finalFreq * modSignalForModulation * fmIndex * args.sampleTime;
        }

        // Total phase increment (can be negative for TZ-FM)
        float finalDeltaPhase = basePhaseInc + fmPhaseInc;

        // Update final phase
        finalPhase += finalDeltaPhase;

        // Get sync mode
        int syncMode = (int)params[SYNC_MODE].getValue();

        // Detect sync trigger BEFORE wrapping
        bool syncTrigger = false;
        if (finalPhase >= 1.0f && prevFinalPhase < 1.0f) {
            syncTrigger = true;
        }
        if (finalPhase < 0.0f && prevFinalPhase >= 0.0f) {
            syncTrigger = true;
        }

        // Apply sync to MOD oscillator
        if (syncTrigger && syncMode > 0) {
            if (syncMode == 2) {
                modPhase = 0.f;  // Hard sync
            } else if (syncMode == 1) {
                if (modPhase > 0.5f) {  // Soft sync
                    modPhase = 0.f;
                }
            }
        }

        // Wrap phase to [0, 1]
        finalPhase = finalPhase - std::floor(finalPhase);

        float finalSignal;

        // Generate FINAL signal
        if (inputs[FINAL_EXT_IN].isConnected()) {
            finalSignal = inputs[FINAL_EXT_IN].getVoltage() / 5.f;
            finalSignal = clamp(finalSignal, -1.f, 1.f);
        } else {
            // Buchla-style "sine" with harmonics
            float fundamental = std::sin(2.f * M_PI * finalPhase);
            float harmonic2 = 0.08f * std::sin(4.f * M_PI * finalPhase);
            float harmonic3 = 0.05f * std::sin(6.f * M_PI * finalPhase);
            finalSignal = (fundamental + harmonic2 + harmonic3) * 0.92f;
        }

        // Store clean sine
        float cleanSine = finalSignal;

        // Get fold amount
        float foldAmount = smoothedHarmonics.process();
        if (inputs[HARMONICS_CV].isConnected()) {
            float foldCV = inputs[HARMONICS_CV].getVoltage() / 10.f;
            foldAmount += foldCV;
            foldAmount = clamp(foldAmount, 0.f, 1.f);
        }

        // TM amount
        float tmAmount = smoothedFoldAmt.process();
        if (inputs[FOLD_AMT_CV].isConnected()) {
            float tmAttenuation = params[FOLD_AMT_ATTEN].getValue();
            float tmCV = inputs[FOLD_AMT_CV].getVoltage() / 10.f;
            tmAmount += tmCV * tmAttenuation;
            tmAmount = clamp(tmAmount, 0.f, 1.f);
        }

        // Calculate modulation amounts for nonlinear processing
        float foldAmountWithMod = foldAmount;
        if (tmAmount > 0.0f) {
            float timbreModulation = (modSignalForModulation * 0.5f + 0.5f) * tmAmount;
            foldAmountWithMod += timbreModulation;
            foldAmountWithMod = clamp(foldAmountWithMod, 0.f, 1.f);
        }

        float rectifyAmount = smoothedOrder.process();
        if (inputs[ORDER_CV].isConnected()) {
            float rectifyCV = inputs[ORDER_CV].getVoltage() / 10.f;
            rectifyAmount += rectifyCV;
            rectifyAmount = clamp(rectifyAmount, 0.f, 1.f);
        }

        float rectModAmount = smoothedSymAmt.process();
        if (inputs[AM_AMT_CV].isConnected()) {
            float rectModAttenuation = params[AM_AMT_ATTEN].getValue();
            float rectModCV = inputs[AM_AMT_CV].getVoltage() / 10.f;
            rectModAmount += rectModCV * rectModAttenuation;
            rectModAmount = clamp(rectModAmount, 0.f, 1.f);
        }

        float rectifyAmountWithMod = rectifyAmount;
        if (rectModAmount > 0.0f) {
            float rectModulation = (modSignalForModulation * 0.5f + 0.5f) * rectModAmount;
            rectifyAmountWithMod += rectModulation;
            rectifyAmountWithMod = clamp(rectifyAmountWithMod, 0.f, 1.f);
        }

        // Apply nonlinear processing with oversampling (like ChoppingKinky)
        if (oversamplingIndex == 0) {
            // No oversampling
            if (foldAmountWithMod > 0.0f) {
                finalSignal = wavefold(finalSignal, foldAmountWithMod);
            }
            finalSignal = asymmetricRectifier(finalSignal, rectifyAmountWithMod);
        } else {
            // With oversampling
            oversampler.upsample(finalSignal);
            float* osBuffer = oversampler.getOSBuffer();

            for (int k = 0; k < oversampler.getOversamplingRatio(); k++) {
                if (foldAmountWithMod > 0.0f) {
                    osBuffer[k] = wavefold(osBuffer[k], foldAmountWithMod);
                }
                osBuffer[k] = asymmetricRectifier(osBuffer[k], rectifyAmountWithMod);
            }

            finalSignal = oversampler.downsample();
        }

        // LPF cutoff
        float lpfCutoffParam = smoothedLpfCutoff.process();
        const float kLpfCutoffMin = 10.0f;
        const float kLpfCutoffMax = 20000.0f;
        float lpfCutoff = kLpfCutoffMin * std::pow(kLpfCutoffMax / kLpfCutoffMin, lpfCutoffParam);

        if (inputs[LPF_CUTOFF_CV].isConnected()) {
            float lpfCV = inputs[LPF_CUTOFF_CV].getVoltage() / 10.f;
            float cvAmount = lpfCV * 2.f - 1.f;
            lpfCutoff *= std::pow(2.f, cvAmount * 2.f);
        }

        lpfCutoff = clamp(lpfCutoff, 20.f, args.sampleRate / 2.f * 0.49f);

        // Apply lowpass filter
        lpFilter.setCutoff(lpfCutoff);
        finalSignal = lpFilter.process(finalSignal);

        // Apply VCA and scale to ±5V
        float finalOutput = finalSignal * 5.f * finalVcaGain;
        float finalSineOutput = cleanSine * 5.f * finalVcaGain;

        // Apply BASS knob
        float bassAmount = smoothedBass.process();
        if (bassAmount > 0.0f) {
            float cleanSineScaled = finalSineOutput * bassAmount * 2.0f;
            finalOutput = finalOutput + cleanSineScaled;

            // Soft clipping
            if (std::abs(finalOutput) > 5.0f) {
                float sign = finalOutput > 0 ? 1.0f : -1.0f;
                float excess = std::abs(finalOutput) - 5.0f;
                finalOutput = sign * (5.0f + std::tanh(excess * 0.3f) * 2.0f);
            }
        }

        // Set outputs
        outputs[MOD_SIGNAL_OUT].setVoltage(modOutputWithVca);
        outputs[FINAL_SINE_OUT].setVoltage(finalSineOutput);
        outputs[FINAL_FINAL_OUT].setVoltage(finalOutput);

        // Trigger light control
        bool trig = !params[TRIG_PARAM].getValue();
        lights[TRIG_LIGHT].setBrightness(trig ? 1.0f : 0.0f);

        // Scope recording (like Observer)
        if (bufferIndex >= SCOPE_BUFFER_SIZE) {
            bool triggered = false;

            if (!trig) {
                triggered = true;
            } else {
                if (scopeTriggers[0].process(rescale(finalSineOutput, 0.f, 0.001f, 0.f, 1.f))) {
                    triggered = true;
                }
            }

            if (triggered) {
                for (int c = 0; c < 16; c++) {
                    scopeTriggers[c].reset();
                }
                bufferIndex = 0;
                frameIndex = 0;
            }
        }

        if (bufferIndex < SCOPE_BUFFER_SIZE) {
            float deltaTime = dsp::exp2_taylor5(-params[SCOPE_TIME].getValue()) / SCOPE_BUFFER_SIZE;
            int frameCount = (int) std::ceil(deltaTime * args.sampleRate);

            float modSample = modOutputWithVca / 5.0f - 1.0f;  // Convert 0-10V to -1 to 1
            float finalSample = finalOutput / 5.0f;  // Convert ±5V to ±1
            currentFinal.min = std::min(currentFinal.min, finalSample);
            currentFinal.max = std::max(currentFinal.max, finalSample);
            currentMod.min = std::min(currentMod.min, modSample);
            currentMod.max = std::max(currentMod.max, modSample);

            if (++frameIndex >= frameCount) {
                frameIndex = 0;
                finalBuffer[bufferIndex] = currentFinal;
                modBuffer[bufferIndex] = currentMod;
                currentFinal = ScopePoint();
                currentMod = ScopePoint();
                bufferIndex++;
            }
        }
    }
};

// Dual-track Scope Display Widget (like VCV version)
struct NIGOQScopeDisplay : LedDisplay {
    NIGOQ* module;

    NIGOQScopeDisplay() {
        box.size = Vec(66, 38.5);
    }

    void drawBackground(const DrawArgs& args) {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(20, 20, 20));
        nvgFill(args.vg);

        // Draw center line
        float centerY = box.size.y / 2;
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, 0, centerY);
        nvgLineTo(args.vg, box.size.x, centerY);
        nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 30));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        // Draw border
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStrokeColor(args.vg, nvgRGB(100, 100, 100));
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgStroke(args.vg);
    }

    void drawWave(const DrawArgs& args, NIGOQ::ScopePoint* buffer, NVGcolor color, float yOffset) {
        if (!module) return;

        nvgSave(args.vg);

        float trackHeight = box.size.y / 2.0f;

        Rect b = Rect(Vec(0, yOffset), Vec(box.size.x, trackHeight));
        nvgScissor(args.vg, RECT_ARGS(b));
        nvgBeginPath(args.vg);

        for (int i = 0; i < NIGOQ::SCOPE_BUFFER_SIZE; i++) {
            const NIGOQ::ScopePoint& point = buffer[i];
            float value = point.max;
            if (!std::isfinite(value))
                value = 0.f;

            Vec p;
            p.x = (float)i / (NIGOQ::SCOPE_BUFFER_SIZE - 1) * b.size.x;
            p.y = b.pos.y + b.size.y * 0.5f * (1.f - value);

            if (i == 0)
                nvgMoveTo(args.vg, p.x, p.y);
            else
                nvgLineTo(args.vg, p.x, p.y);
        }

        nvgStrokeColor(args.vg, color);
        nvgStrokeWidth(args.vg, 1.0f);
        nvgStroke(args.vg);
        nvgResetScissor(args.vg);
        nvgRestore(args.vg);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        drawBackground(args);

        if (!module) return;

        // Draw FINAL trace (top half, pink)
        drawWave(args, module->finalBuffer, nvgRGB(255, 133, 133), 0);

        // Draw MOD trace (bottom half, cyan)
        drawWave(args, module->modBuffer, nvgRGB(133, 200, 255), box.size.y / 2.0f);
    }

    void onDragMove(const event::DragMove& e) override {
        ModuleWidget* mw = getAncestorOfType<ModuleWidget>();
        if (!mw) return;

        ParamWidget* timeParam = mw->getParam(NIGOQ::SCOPE_TIME);
        if (!timeParam) return;

        ParamQuantity* pq = timeParam->getParamQuantity();
        if (!pq) return;

        float sensitivity = 0.01f;
        float deltaValue = -e.mouseDelta.y * sensitivity;
        pq->setValue(pq->getValue() + deltaValue);
        e.consume(this);
    }
};

struct NIGOQWidget : ModuleWidget {
    NIGOQWidget(NIGOQ* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "NIGOQ.png")));
        box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        // Trigger input (top right)
        addInput(createInputCentered<PJ301MPort>(Vec(165, 55), module, NIGOQ::TRIG_IN));

        // Main frequency controls
        addParam(createParamCentered<RoundBlackKnob>(Vec(55, 55), module, NIGOQ::MOD_FREQ));
        addParam(createParamCentered<RoundBlackKnob>(Vec(125, 55), module, NIGOQ::FINAL_FREQ));

        // Wave type
        addParam(createParamCentered<Trimpot>(Vec(20, 55), module, NIGOQ::MOD_WAVE));
        addInput(createInputCentered<PJ301MPort>(Vec(20, 95), module, NIGOQ::MOD_WAVE_CV));

        // External inputs
        addInput(createInputCentered<PJ301MPort>(Vec(55, 92), module, NIGOQ::MOD_EXT_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(125, 92), module, NIGOQ::FINAL_EXT_IN));

        // Filter and processing
        addParam(createParamCentered<RoundBlackKnob>(Vec(125, 130), module, NIGOQ::LPF_CUTOFF));
        addInput(createInputCentered<PJ301MPort>(Vec(165, 130), module, NIGOQ::LPF_CUTOFF_CV));

        addParam(createParamCentered<RoundBlackKnob>(Vec(125, 175), module, NIGOQ::ORDER));
        addInput(createInputCentered<PJ301MPort>(Vec(165, 175), module, NIGOQ::ORDER_CV));

        addParam(createParamCentered<RoundBlackKnob>(Vec(125, 220), module, NIGOQ::HARMONICS));
        addInput(createInputCentered<PJ301MPort>(Vec(165, 220), module, NIGOQ::HARMONICS_CV));

        // Modulation controls
        addParam(createParamCentered<Trimpot>(Vec(55, 130), module, NIGOQ::FM_AMT_ATTEN));
        addInput(createInputCentered<PJ301MPort>(Vec(20, 130), module, NIGOQ::FM_AMT_CV));
        addParam(createParamCentered<RoundBlackKnob>(Vec(90, 130), module, NIGOQ::FM_AMT));

        addParam(createParamCentered<Trimpot>(Vec(55, 175), module, NIGOQ::AM_AMT_ATTEN));
        addInput(createInputCentered<PJ301MPort>(Vec(20, 175), module, NIGOQ::AM_AMT_CV));
        addParam(createParamCentered<RoundBlackKnob>(Vec(90, 175), module, NIGOQ::AM_AMT));

        addParam(createParamCentered<Trimpot>(Vec(55, 220), module, NIGOQ::FOLD_AMT_ATTEN));
        addInput(createInputCentered<PJ301MPort>(Vec(20, 220), module, NIGOQ::FOLD_AMT_CV));
        addParam(createParamCentered<RoundBlackKnob>(Vec(90, 220), module, NIGOQ::FOLD_AMT));

        // Additional controls
        addParam(createParamCentered<Trimpot>(Vec(165, 90), module, NIGOQ::DECAY));
        addParam(createParamCentered<RoundBlackKnob>(Vec(165, 265), module, NIGOQ::BASS));
        addParam(createParamCentered<CKSSThree>(Vec(90, 85), module, NIGOQ::SYNC_MODE));
        addParam(createParamCentered<Trimpot>(Vec(145, 270), module, NIGOQ::SCOPE_TIME));

        // Attack time (invisible, not shown on panel but stored in patch)
        addParam(createParamCentered<Trimpot>(Vec(0, 0), module, NIGOQ::ATTACK_TIME));

        // CV inputs and FM
        addParam(createParamCentered<Trimpot>(Vec(77, 310), module, NIGOQ::MOD_FM_ATTEN));
        addParam(createParamCentered<Trimpot>(Vec(108, 310), module, NIGOQ::FINAL_FM_ATTEN));

        addInput(createInputCentered<PJ301MPort>(Vec(20, 310), module, NIGOQ::MOD_1VOCT));
        addInput(createInputCentered<PJ301MPort>(Vec(50, 310), module, NIGOQ::MOD_FM_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(135, 310), module, NIGOQ::FINAL_FM_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(165, 310), module, NIGOQ::FINAL_1VOCT));

        // Scope display
        NIGOQScopeDisplay* scopeDisplay = new NIGOQScopeDisplay();
        scopeDisplay->box.pos = Vec(40, 335);
        scopeDisplay->module = module;
        addChild(scopeDisplay);

        // Trigger button and light (next to scope)
        addParam(createParamCentered<TL1105>(Vec(110, 345), module, NIGOQ::TRIG_PARAM));
        addChild(createLightCentered<MediumLight<RedLight>>(Vec(110, 330), module, NIGOQ::TRIG_LIGHT));

        // Outputs
        addOutput(createOutputCentered<PJ301MPort>(Vec(20, 360), module, NIGOQ::MOD_SIGNAL_OUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(135, 360), module, NIGOQ::FINAL_SINE_OUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(165, 360), module, NIGOQ::FINAL_FINAL_OUT));
    }

    void appendContextMenu(Menu* menu) override {
        NIGOQ* module = dynamic_cast<NIGOQ*>(this->module);
        if (!module) return;

        menu->addChild(new MenuSeparator());
        menu->addChild(createMenuLabel("Oversampling"));
        menu->addChild(createIndexSubmenuItem("Oversampling",
            {"Off", "x2", "x4", "x8", "x16"},
            [=]() { return module->oversamplingIndex; },
            [=](int mode) {
                module->oversamplingIndex = mode;
                module->oversampler.setOversamplingIndex(mode);
                module->oversampler.reset(APP->engine->getSampleRate());
            }
        ));
    }
};

Model* modelNIGOQ = createModel<NIGOQ, NIGOQWidget>("NIGOQ");
