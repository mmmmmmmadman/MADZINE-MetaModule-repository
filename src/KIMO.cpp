#include "plugin.hpp"

struct KimoAccentParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int value = (int)std::round(getValue());
        return string::f("%d step", value);
    }
};

static void generateTechnoEuclideanRhythm(bool pattern[], int length, int fill, int shift) {
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

struct UnifiedEnvelope {
    dsp::SchmittTrigger trigTrigger;
    dsp::PulseGenerator trigPulse;
    float phase = 0.f;
    bool gateState = false;
    static constexpr float ATTACK_TIME = 0.001f;
    
    void reset() {
        trigTrigger.reset();
        trigPulse.reset();
        phase = 0.f;
        gateState = false;
    }
    
    float smoothDecayEnvelope(float t, float totalTime, float shapeParam) {
        if (t >= totalTime) return 0.f;
        
        float normalizedT = t / totalTime;
        
        float frontK = -0.9f + shapeParam * 0.5f;
        float backK = -1.0f + 1.6f * std::pow(shapeParam, 0.3f);
        
        float transition = normalizedT * normalizedT * (3.f - 2.f * normalizedT);
        float k = frontK + (backK - frontK) * transition;
        
        float absT = std::abs(normalizedT);
        float denominator = k - 2.f * k * absT + 1.f;
        if (std::abs(denominator) < 1e-10f) {
            return 1.f - normalizedT;
        }
        
        float curveResult = (normalizedT - k * normalizedT) / denominator;
        return 1.f - curveResult;
    }
    
    float process(float sampleTime, float triggerVoltage, float decayTime, float shapeParam) {
        bool triggered = trigTrigger.process(triggerVoltage, 0.1f, 2.f);
        
        if (triggered) {
            phase = 0.f;
            gateState = true;
            trigPulse.trigger(0.03f);
        }
        
        float envOutput = 0.f;
        
        if (gateState) {
            if (phase < ATTACK_TIME) {
                envOutput = phase / ATTACK_TIME;
            } else {
                float decayPhase = phase - ATTACK_TIME;
                
                if (decayPhase >= decayTime) {
                    gateState = false;
                    envOutput = 0.f;
                } else {
                    envOutput = smoothDecayEnvelope(decayPhase, decayTime, shapeParam);
                }
            }
            
            phase += sampleTime;
        }
        
        return clamp(envOutput, 0.f, 1.f);
    }
    
    float getTrigger(float sampleTime) {
        return trigPulse.process(sampleTime) ? 10.0f : 0.0f;
    }
};

struct LinearEnvelope {
    dsp::SchmittTrigger trigTrigger;
    dsp::PulseGenerator trigPulse;
    float phase = 0.f;
    bool gateState = false;
    static constexpr float ATTACK_TIME = 0.001f;
    
    void reset() {
        trigTrigger.reset();
        trigPulse.reset();
        phase = 0.f;
        gateState = false;
    }
    
    float process(float sampleTime, float triggerVoltage, float decayTime) {
        bool triggered = trigTrigger.process(triggerVoltage, 0.1f, 2.f);
        
        if (triggered) {
            phase = 0.f;
            gateState = true;
            trigPulse.trigger(0.03f);
        }
        
        float envOutput = 0.f;
        
        if (gateState) {
            if (phase < ATTACK_TIME) {
                envOutput = phase / ATTACK_TIME;
            } else {
                float decayPhase = phase - ATTACK_TIME;
                
                if (decayPhase >= decayTime) {
                    gateState = false;
                    envOutput = 0.f;
                } else {
                    envOutput = 1.0f - (decayPhase / decayTime);
                }
            }
            
            phase += sampleTime;
        }
        
        return clamp(envOutput, 0.f, 1.f);
    }
    
    float getTrigger(float sampleTime) {
        return trigPulse.process(sampleTime) ? 10.0f : 0.0f;
    }
};

struct BasicSineVCO {
    float phase = 0.0f;
    float sampleRate = 44100.0f;
    
    void setSampleRate(float sr) {
        sampleRate = sr;
    }
    
    float process(float freq_hz, float fm_cv, float saturation = 1.0f) {
        float modulated_freq = freq_hz * std::pow(2.0f, fm_cv);
        modulated_freq = clamp(modulated_freq, 1.0f, sampleRate * 0.45f);
        
        float delta_phase = modulated_freq / sampleRate;
        
        phase += delta_phase;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
        
        float sine_wave = std::sin(2.0f * M_PI * phase);
        
        if (saturation > 1.0f) {
            sine_wave = std::tanh(sine_wave * saturation) / std::tanh(saturation);
        }
        
        return sine_wave * 5.0f;
    }
};

struct KIMO : Module {
    enum ParamId {
        FILL_PARAM,
        ACCENT_PARAM,
        ACCENT_DELAY_PARAM,
        TUNE_PARAM,
        FM_PARAM,
        PUNCH_PARAM,
        DECAY_PARAM,
        SHAPE_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CLK_INPUT,
        TUNE_CV_INPUT,
        FM_CV_INPUT,
        PUNCH_CV_INPUT,
        DECAY_CV_INPUT,
        FILL_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        VCA_ENV_OUTPUT,
        FM_ENV_OUTPUT,
        ACCENT_ENV_OUTPUT,
        AUDIO_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    dsp::SchmittTrigger clockTrigger;
    
    float globalClockSeconds = 0.5f;
    float secondsSinceLastClock = -1.0f;
    static constexpr int GLOBAL_LENGTH = 16;
    
    BasicSineVCO kickVCO;
    
    struct QuarterNoteClock {
        int currentStep = 0;
        int shiftAmount = 1;
        dsp::PulseGenerator trigPulse;
        
        void reset() {
            currentStep = 0;
        }
        
        bool processStep(bool globalClockTriggered, int shift) {
            shiftAmount = shift;
            if (globalClockTriggered) {
                currentStep = (currentStep + 1) % 4;
                
                int targetStep = shiftAmount % 4;
                if (currentStep == targetStep) {
                    trigPulse.trigger(0.01f);
                    return true;
                }
            }
            return false;
        }
        
        float getTrigger(float sampleTime) {
            return trigPulse.process(sampleTime) ? 10.0f : 0.0f;
        }
    };

    struct TrackState {
        int currentStep = 0;
        int length = GLOBAL_LENGTH;
        int fill = 4;
        int shift = 0;
        bool pattern[32];
        bool gateState = false;
        dsp::PulseGenerator trigPulse;
        
        UnifiedEnvelope envelope;
        LinearEnvelope vcaEnvelope;

        void reset() {
            currentStep = 0;
            for (int i = 0; i < 32; ++i) {
                pattern[i] = false;
            }
            gateState = false;
            envelope.reset();
            vcaEnvelope.reset();
        }
        
        void stepTrack() {
            currentStep = (currentStep + 1) % length;
            gateState = pattern[currentStep];
            if (gateState) {
                trigPulse.trigger(0.01f);
            }
        }
    };
    
    TrackState track;
    QuarterNoteClock quarterClock;
    UnifiedEnvelope accentVCA;

    KIMO() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        
        configInput(CLK_INPUT, "Clock");
        configInput(TUNE_CV_INPUT, "Tune CV");
        configInput(FM_CV_INPUT, "FM CV");
        configInput(PUNCH_CV_INPUT, "Punch CV");
        configInput(DECAY_CV_INPUT, "Decay CV");
        configInput(FILL_CV_INPUT, "Fill CV");

        configParam(FILL_PARAM, 0.0f, 100.0f, 71.20001220703125f, "Fill", "%");
        configParam(ACCENT_PARAM, 1.0f, 7.0f, 3.0f, "Accent");
        getParamQuantity(ACCENT_PARAM)->snapEnabled = true;
        delete paramQuantities[ACCENT_PARAM];
        paramQuantities[ACCENT_PARAM] = new KimoAccentParamQuantity;
        paramQuantities[ACCENT_PARAM]->module = this;
        paramQuantities[ACCENT_PARAM]->paramId = ACCENT_PARAM;
        paramQuantities[ACCENT_PARAM]->minValue = 1.0f;
        paramQuantities[ACCENT_PARAM]->maxValue = 7.0f;
        paramQuantities[ACCENT_PARAM]->defaultValue = 3.0f;
        paramQuantities[ACCENT_PARAM]->name = "Accent";
        paramQuantities[ACCENT_PARAM]->snapEnabled = true;

        configParam(ACCENT_DELAY_PARAM, 0.01f, 2.0f, 0.54331988096237183f, "Accent Delay", " s");
        configParam(TUNE_PARAM, std::log2(24.0f), std::log2(500.0f), 4.5849623680114746f, "Tune", " Hz", 2.f);
        configParam(FM_PARAM, 0.0f, 1.0f, 0.12400007992982864f, "FM Amount");
        configParam(PUNCH_PARAM, 0.0f, 1.0f, 0.67500001192092896f, "Punch Amount");
        configParam(DECAY_PARAM, std::log(0.01f), std::log(2.0f), -3.180246114730835f, "Decay", " s", 2.718281828f);
        configParam(SHAPE_PARAM, 0.0f, 0.99f, 0.11884991824626923f, "Shape");
        
        configOutput(VCA_ENV_OUTPUT, "VCA Envelope");
        configOutput(FM_ENV_OUTPUT, "FM Envelope");
        configOutput(ACCENT_ENV_OUTPUT, "Accent Envelope");
        configOutput(AUDIO_OUTPUT, "Audio");
        
        kickVCO.setSampleRate(44100.0f);
    }

    void onSampleRateChange() override {
        float sr = APP->engine->getSampleRate();
        kickVCO.setSampleRate(sr);
    }

    void onReset() override {
        secondsSinceLastClock = -1.0f;
        globalClockSeconds = 0.5f;
        track.reset();
        quarterClock.reset();
        accentVCA.reset();
    }

    void process(const ProcessArgs& args) override {
        bool globalClockActive = inputs[CLK_INPUT].isConnected();
        bool globalClockTriggered = false;
        
        if (globalClockActive) {
            float clockVoltage = inputs[CLK_INPUT].getVoltage();
            globalClockTriggered = clockTrigger.process(clockVoltage);
        }
        
        if (globalClockTriggered) {
            if (secondsSinceLastClock > 0.0f) {
                globalClockSeconds = secondsSinceLastClock;
                globalClockSeconds = clamp(globalClockSeconds, 0.01f, 10.0f);
            }
            secondsSinceLastClock = 0.0f;
        }
        
        if (secondsSinceLastClock >= 0.0f) {
            secondsSinceLastClock += args.sampleTime;
        }

        int accentShift = (int)std::round(params[ACCENT_PARAM].getValue());
        bool accentTriggered = quarterClock.processStep(globalClockTriggered, accentShift);
        float accentTrigger = quarterClock.getTrigger(args.sampleTime);

        track.length = GLOBAL_LENGTH;

        float fillParam = params[FILL_PARAM].getValue();
        if (inputs[FILL_CV_INPUT].isConnected()) {
            fillParam += inputs[FILL_CV_INPUT].getVoltage() * 10.0f;
        }
        float fillPercentage = clamp(fillParam, 0.0f, 100.0f);
        track.fill = (int)std::round((fillPercentage / 100.0f) * track.length);

        track.shift = 0;

        generateTechnoEuclideanRhythm(track.pattern, track.length, track.fill, track.shift);

        if (globalClockTriggered && globalClockActive) {
            track.stepTrack();
        }
        
        float decayParam = std::exp(params[DECAY_PARAM].getValue());
        if (inputs[DECAY_CV_INPUT].isConnected()) {
            decayParam += inputs[DECAY_CV_INPUT].getVoltage() / 10.0f;
            decayParam = clamp(decayParam, 0.01f, 2.0f);
        }
        float shapeParam = params[SHAPE_PARAM].getValue();
        
        float triggerOutput = track.trigPulse.process(args.sampleTime) ? 10.0f : 0.0f;
        float envelopeOutput = track.envelope.process(args.sampleTime, triggerOutput, decayParam, shapeParam);
        
        float fmAmount = params[FM_PARAM].getValue();
        if (inputs[FM_CV_INPUT].isConnected()) {
            fmAmount += inputs[FM_CV_INPUT].getVoltage() / 10.0f;
            fmAmount = clamp(fmAmount, 0.0f, 1.0f);
        }
        
        float freqParam = std::pow(2.0f, params[TUNE_PARAM].getValue());
        if (inputs[TUNE_CV_INPUT].isConnected()) {
            float freqCV = params[TUNE_PARAM].getValue() + inputs[TUNE_CV_INPUT].getVoltage();
            freqParam = std::pow(2.0f, freqCV);
            freqParam = clamp(freqParam, std::pow(2.0f, std::log2(24.0f)), std::pow(2.0f, std::log2(500.0f)));
        }
        
        float punchAmount = params[PUNCH_PARAM].getValue();
        if (inputs[PUNCH_CV_INPUT].isConnected()) {
            punchAmount += inputs[PUNCH_CV_INPUT].getVoltage() / 10.0f;
            punchAmount = clamp(punchAmount, 0.0f, 1.0f);
        }
        
        float envelopeFM = envelopeOutput * fmAmount * 20.0f;
        float punchSaturation = 1.0f + (punchAmount * 2.0f);
        float audioOutput = kickVCO.process(freqParam, envelopeFM, punchSaturation);
        
        float vcaEnvelopeOutput = track.vcaEnvelope.process(args.sampleTime, triggerOutput, decayParam);
        
        float accentDelayParam = params[ACCENT_DELAY_PARAM].getValue();
        float accentVCAOutput = accentVCA.process(args.sampleTime, accentTrigger, accentDelayParam, 0.5f);
        
        float finalAudioOutput = audioOutput * vcaEnvelopeOutput * accentVCAOutput * 1.8f;
        
        outputs[VCA_ENV_OUTPUT].setVoltage(vcaEnvelopeOutput * 10.0f);
        outputs[FM_ENV_OUTPUT].setVoltage(envelopeOutput * 10.0f);
        outputs[ACCENT_ENV_OUTPUT].setVoltage(accentVCAOutput * 10.0f);
        outputs[AUDIO_OUTPUT].setVoltage(finalAudioOutput);
    }
};

struct KIMOWidget : ModuleWidget {
    KIMOWidget(KIMO* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "KIMO.png")));
        
        box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        addInput(createInputCentered<PJ301MPort>(Vec(15, 63), module, KIMO::CLK_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(Vec(45, 63), module, KIMO::FILL_PARAM));

        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(15, 105), module, KIMO::ACCENT_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(45, 105), module, KIMO::ACCENT_DELAY_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(Vec(15, 147), module, KIMO::TUNE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(45, 147), module, KIMO::FM_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(Vec(15, 189), module, KIMO::PUNCH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(45, 189), module, KIMO::DECAY_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(Vec(15, 231), module, KIMO::SHAPE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 231), module, KIMO::FILL_CV_INPUT));

        addInput(createInputCentered<PJ301MPort>(Vec(15, 272), module, KIMO::FM_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 272), module, KIMO::TUNE_CV_INPUT));

        addInput(createInputCentered<PJ301MPort>(Vec(15, 308), module, KIMO::DECAY_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(45, 308), module, KIMO::PUNCH_CV_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(Vec(15, 343), module, KIMO::VCA_ENV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(45, 343), module, KIMO::FM_ENV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(15, 368), module, KIMO::ACCENT_ENV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(45, 368), module, KIMO::AUDIO_OUTPUT));
    }
};

Model* modelKIMO = createModel<KIMO, KIMOWidget>("KIMO");