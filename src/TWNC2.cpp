#include "plugin.hpp"

struct BasicBandpassFilter {
    float x1 = 0.0f, x2 = 0.0f, x3 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f, y3 = 0.0f;
    float sampleRate = 44100.0f;
    float lastFreq = 1000.0f;
    float lastQ = 0.5f;
    
    void setSampleRate(float sr) {
        sampleRate = sr;
    }
    
    void setFrequency(float freq, float q = 0.5f) {
        lastFreq = clamp(freq, 20.0f, sampleRate * 0.45f);
        lastQ = q;
    }
    
    float process(float input) {
        float omega = 2.0f * M_PI * lastFreq / sampleRate;
        float sin_omega = std::sin(omega);
        float cos_omega = std::cos(omega);
        float q = lastQ;
        
        if (q < 0.1f) q = 0.1f;
        
        float alpha = sin_omega / (2.0f * q);
        float norm = 1.0f / (1.0f + alpha);
        float b0 = alpha * norm;
        float b2 = -alpha * norm;
        float a1 = -2.0f * cos_omega * norm;
        float a2 = (1.0f - alpha) * norm;
        
        float output = b0 * input + b2 * x2 - a1 * y1 - a2 * y2;
        
        if (q > 1.5f) {
            float pole3_cutoff = lastFreq * 1.2f;
            float omega3 = 2.0f * M_PI * pole3_cutoff / sampleRate;
            float a3 = -std::cos(omega3);
            float b3 = (1.0f - std::cos(omega3)) / 2.0f;
            
            float stage3 = b3 * output + b3 * x3 - a3 * y3;
            x3 = output;
            y3 = stage3;
            
            float blend = (q - 1.5f) / 1.5f;
            output = output * (1.0f - blend) + stage3 * blend;
        }
        
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;
        
        return output;
    }
    
    void reset() {
        x1 = x2 = x3 = y1 = y2 = y3 = 0.0f;
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

struct TWNC2 : Module {
    enum ParamId {
        KICK_VOLUME_PARAM,
        KICK_FREQ_PARAM,
        KICK_FM_AMT_PARAM,
        KICK_PUNCH_PARAM,
        SNARE_VOLUME_PARAM,
        SNARE_FREQ_PARAM,
        SNARE_NOISE_TONE_PARAM,
        SNARE_NOISE_MIX_PARAM,
        HATS_VOLUME_PARAM,
        HATS_TONE_PARAM,
        HATS_DECAY_PARAM,
        DUCK_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        KICK_ENV_INPUT,
        KICK_ACCENT_INPUT,
        KICK_FREQ_CV_INPUT,
        KICK_FM_CV_INPUT,
        KICK_PUNCH_CV_INPUT,
        SNARE_ENV_INPUT,
        SNARE_NOISE_MIX_CV_INPUT,
        HATS_ENV_INPUT,
        HATS_DECAY_CV_INPUT,
        EXTERNAL_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        KICK_OUTPUT,
        SNARE_OUTPUT,
        HATS_OUTPUT1,
        MIX_OUTPUT_L,
        MIX_OUTPUT_R,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };
    
    BasicSineVCO kickVCO;
    BasicSineVCO snareVCO;
    BasicBandpassFilter snareNoiseFilter;
    BasicBandpassFilter hatsFilter;
    
    struct HatsOscillator {
        float phases[6] = {0.0f};
        float sampleRate = 44100.0f;
        float offsets[6] = {100.0f, 250.0f, 400.0f, 550.0f, 600.0f, 1000.0f};
        
        void setSampleRate(float sr) {
            sampleRate = sr;
        }
        
        float process(float baseFreq) {
            float output = 0.0f;
            for (int i = 0; i < 6; i++) {
                float freq = baseFreq + offsets[i];
                float phaseInc = freq / sampleRate;
                phases[i] += phaseInc;
                if (phases[i] >= 1.0f) phases[i] -= 1.0f;
                
                float triangle;
                if (phases[i] < 0.5f) {
                    triangle = 4.0f * phases[i] - 1.0f;
                } else {
                    triangle = 3.0f - 4.0f * phases[i];
                }
                output += triangle * 5.0f/6.0f;
            }
            return output;
        }
    } hatsOsc;
    
    struct DelayLine {
        static const int maxDelay = 1440;
        float buffer[maxDelay] = {0.0f};
        int writePos = 0;
        float sampleRate = 44100.0f;
        
        void setSampleRate(float sr) {
            sampleRate = sr;
        }
        
        float process(float input, float delayMs) {
            int delaySamples = (int)(delayMs * sampleRate / 1000.0f);
            delaySamples = clamp(delaySamples, 0, maxDelay - 1);
            
            buffer[writePos] = input;
            int readPos = (writePos - delaySamples + maxDelay) % maxDelay;
            float output = buffer[readPos];
            
            writePos = (writePos + 1) % maxDelay;
            return output;
        }
    } hatsDelay;

    TWNC2() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        
        configInput(KICK_ENV_INPUT, "Kick Envelope CV");
        configInput(KICK_ACCENT_INPUT, "Kick Accent CV");
        configInput(KICK_FREQ_CV_INPUT, "Kick Frequency CV");
        configInput(KICK_FM_CV_INPUT, "Kick FM CV");
        configInput(KICK_PUNCH_CV_INPUT, "Kick Punch CV");
        configInput(SNARE_ENV_INPUT, "Snare Envelope CV");
        configInput(SNARE_NOISE_MIX_CV_INPUT, "Snare Noise Mix CV");
        configInput(HATS_ENV_INPUT, "Hats Envelope CV");
        configInput(HATS_DECAY_CV_INPUT, "Hats Decay CV");
        configInput(EXTERNAL_INPUT, "External Input");
        
        configParam(KICK_VOLUME_PARAM, 0.0f, 1.0f, 1.0f, "Kick Volume");
        configParam(KICK_FREQ_PARAM, std::log2(24.0f), std::log2(500.0f), 4.5849623680114746f, "Kick Frequency", " Hz", 2.f);
        configParam(KICK_FM_AMT_PARAM, 0.0f, 1.0f, 0.15700007975101471f, "Kick FM Amount");
        configParam(KICK_PUNCH_PARAM, 0.0f, 1.0f, 0.16800001263618469f, "Kick Punch Amount");

        configParam(SNARE_VOLUME_PARAM, 0.0f, 1.0f, 1.0f, "Snare Volume");
        configParam(SNARE_FREQ_PARAM, std::log2(100.0f), std::log2(300.0f), 6.9100170135498047f, "Snare Frequency", " Hz", 2.f);
        configParam(SNARE_NOISE_TONE_PARAM, 0.0f, 1.0f, 0.71700006723403931f, "Snare Noise Tone");
        configParam(SNARE_NOISE_MIX_PARAM, 0.0f, 1.0f, 0.28799989819526672f, "Snare Noise Mix");

        configParam(HATS_VOLUME_PARAM, 0.0f, 1.0f, 1.0f, "Hats Volume");
        configParam(HATS_TONE_PARAM, 0.0f, 1.0f, 0.9649999737739563f, "Hats Tone");
        configParam(HATS_DECAY_PARAM, 0.0f, 1.0f, 0.0f, "Hats Decay");
        configParam(DUCK_PARAM, 0.0f, 1.0f, 0.0f, "Duck Amount");
        
        configOutput(KICK_OUTPUT, "Kick Audio");
        configOutput(SNARE_OUTPUT, "Snare Audio");
        configOutput(HATS_OUTPUT1, "Hats Audio 1");
        configOutput(MIX_OUTPUT_L, "Mix Output L");
        configOutput(MIX_OUTPUT_R, "Mix Output R");
        
        kickVCO.setSampleRate(44100.0f);
        snareVCO.setSampleRate(44100.0f);
        snareNoiseFilter.setSampleRate(44100.0f);
        hatsFilter.setSampleRate(44100.0f);
        hatsOsc.setSampleRate(44100.0f);
        hatsDelay.setSampleRate(44100.0f);
    }

    void onSampleRateChange() override {
        float sr = APP->engine->getSampleRate();
        kickVCO.setSampleRate(sr);
        snareVCO.setSampleRate(sr);
        snareNoiseFilter.setSampleRate(sr);
        hatsFilter.setSampleRate(sr);
        hatsOsc.setSampleRate(sr);
        hatsDelay.setSampleRate(sr);
    }

    void onReset() override {
    }

    void process(const ProcessArgs& args) override {
        float kickEnvCV = clamp(inputs[KICK_ENV_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
        float kickAccentCV = clamp(inputs[KICK_ACCENT_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
        float duckAmount = params[DUCK_PARAM].getValue();
        float sidechainCV = 1.0f - (kickAccentCV * duckAmount * 3.0f);
        
        float kickVolumeParam = params[KICK_VOLUME_PARAM].getValue();
        float kickPunchAmount = params[KICK_PUNCH_PARAM].getValue();
        if (inputs[KICK_PUNCH_CV_INPUT].isConnected()) {
            kickPunchAmount += inputs[KICK_PUNCH_CV_INPUT].getVoltage() / 10.0f;
            kickPunchAmount = clamp(kickPunchAmount, 0.0f, 1.0f);
        }
        
        float kickFmAmount = params[KICK_FM_AMT_PARAM].getValue() * 20.0f;
        if (inputs[KICK_FM_CV_INPUT].isConnected()) {
            kickFmAmount += (inputs[KICK_FM_CV_INPUT].getVoltage() / 10.0f) * 20.0f;
            kickFmAmount = clamp(kickFmAmount, 0.0f, 20.0f);
        }
        
        float kickFreqParam = std::pow(2.0f, params[KICK_FREQ_PARAM].getValue());
        if (inputs[KICK_FREQ_CV_INPUT].isConnected()) {
            float freqCV = params[KICK_FREQ_PARAM].getValue() + inputs[KICK_FREQ_CV_INPUT].getVoltage();
            kickFreqParam = std::pow(2.0f, freqCV);
            kickFreqParam = clamp(kickFreqParam, std::pow(2.0f, std::log2(24.0f)), std::pow(2.0f, std::log2(500.0f)));
        }
        
        float kickFmCV = kickEnvCV * kickEnvCV;
        float kickVcaCV = std::sqrt(kickEnvCV);
        
        float kickEnvelopeFM = kickFmCV * kickFmAmount;
        float kickSaturation = 1.0f + (kickPunchAmount * 4.0f);
        float kickAudioOutput = kickVCO.process(kickFreqParam, kickEnvelopeFM, kickSaturation);
        float kickFinalOutput = kickAudioOutput * kickVcaCV * kickAccentCV * kickVolumeParam * 0.8f;
        
        float snareEnvCV = clamp(inputs[SNARE_ENV_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
        
        float snareVolumeParam = params[SNARE_VOLUME_PARAM].getValue();
        float snareNoiseTone = params[SNARE_NOISE_TONE_PARAM].getValue();
        float snareNoiseMix = params[SNARE_NOISE_MIX_PARAM].getValue();
        if (inputs[SNARE_NOISE_MIX_CV_INPUT].isConnected()) {
            snareNoiseMix += inputs[SNARE_NOISE_MIX_CV_INPUT].getVoltage() / 10.0f;
            snareNoiseMix = clamp(snareNoiseMix, 0.0f, 1.0f);
        }
        
        float snareBaseFreq = std::pow(2.0f, params[SNARE_FREQ_PARAM].getValue());
        float snareVcaCV = std::sqrt(snareEnvCV);
        
        float snareBodyOutput = snareVCO.process(snareBaseFreq, 0.0f) * 0.75f;
        
        float snareNoiseRaw = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
        float baseFilterFreq = snareBaseFreq * 5.0f;
        float noiseFilterFreq = baseFilterFreq + (snareNoiseTone * 5000.0f) + (snareEnvCV * 2000.0f);
        
        snareNoiseFilter.setFrequency(noiseFilterFreq, 0.5f);
        float snareNoiseFiltered = snareNoiseFilter.process(snareNoiseRaw) * 4.0f;
        
        float snareMixedOutput = (snareBodyOutput * (1.0f - snareNoiseMix)) + (snareNoiseFiltered * snareNoiseMix);
        float sidechain = 0.02f + (sidechainCV * 0.98f);
        float snareFinalOutput = snareMixedOutput * snareVcaCV * snareVolumeParam * sidechain * 4.0f;
        
        float bitRange = 1024.0f;
        
        auto processLimiter = [](float input) -> float {
            const float threshold = 5.0f;
            if (input > threshold) {
                return threshold + std::tanh((input - threshold) * 0.5f) * 2.0f;
            } else if (input < -threshold) {
                return -threshold + std::tanh((input + threshold) * 0.5f) * 2.0f;
            }
            return input;
        };
        
        float kickQuantized = std::round(kickFinalOutput * bitRange) / bitRange;
        float snareQuantized = std::round(snareFinalOutput * bitRange) / bitRange;
        
        outputs[KICK_OUTPUT].setVoltage(kickQuantized);
        outputs[SNARE_OUTPUT].setVoltage(snareQuantized);
        
        float hatsEnvCV = clamp(inputs[HATS_ENV_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
        
        float hatsVolumeParam = params[HATS_VOLUME_PARAM].getValue();
        float hatsTone = params[HATS_TONE_PARAM].getValue();
        float hatsSpread = 20.0f;
        float hatsDecay = params[HATS_DECAY_PARAM].getValue();
        if (inputs[HATS_DECAY_CV_INPUT].isConnected()) {
            hatsDecay += inputs[HATS_DECAY_CV_INPUT].getVoltage() / 10.0f;
            hatsDecay = clamp(hatsDecay, 0.0f, 1.0f);
        }
        
        float hatsBaseFreq = 1000.0f + (hatsTone * 4500.0f);
        float hatsSquareWave = hatsOsc.process(hatsBaseFreq);
        
        float hatsFilterFreq = hatsBaseFreq + (hatsTone * 4000.0f);
        hatsFilter.setFrequency(hatsFilterFreq, 0.5f);
        float hatsFiltered = hatsFilter.process(hatsSquareWave);
        
        float hatsNoiseRaw = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
        float hatsNoiseFiltered = snareNoiseFilter.process(hatsNoiseRaw);
        float hatsNoiseAmount = hatsDecay * 0.8f;
        
        float hatsMixed = hatsFiltered + (hatsNoiseFiltered * hatsNoiseAmount);
        
        float hatsVcaDecay = 2.0f - (hatsDecay * 1.5f);
        float hatsVcaCV = std::pow(hatsEnvCV, hatsVcaDecay);
        
        float hatsReducedSidechain = 0.8f + (sidechainCV * 0.2f);
        float hatsFinalOutput = hatsMixed * hatsVcaCV * hatsVolumeParam * hatsReducedSidechain * 0.7f;
        
        float hatsQuantized = std::round(hatsFinalOutput * bitRange) / bitRange;
        float hatsDelayed = hatsDelay.process(hatsQuantized, hatsSpread);
        
        outputs[HATS_OUTPUT1].setVoltage(hatsQuantized);
        
        float externalInput = inputs[EXTERNAL_INPUT].getVoltage();
        externalInput *= sidechain;
        
        float mixOutputL = kickQuantized + snareQuantized + hatsQuantized + externalInput;
        mixOutputL = processLimiter(mixOutputL);
        
        float mixOutputR = kickQuantized + snareQuantized + hatsDelayed + externalInput;
        mixOutputR = processLimiter(mixOutputR);

        outputs[MIX_OUTPUT_L].setVoltage(mixOutputL);
        outputs[MIX_OUTPUT_R].setVoltage(mixOutputR);
    }
};

struct TWNC2Widget : ModuleWidget {
    TWNC2Widget(TWNC2* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "TWNC2.png")));
        
        box.size = Vec(8 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        addParam(createParamCentered<RoundBlackKnob>(Vec(20, 69), module, TWNC2::KICK_VOLUME_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(60, 69), module, TWNC2::KICK_ENV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(100, 69), module, TWNC2::KICK_ACCENT_INPUT));
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(20, 106), module, TWNC2::KICK_FREQ_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(60, 106), module, TWNC2::KICK_FM_AMT_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(100, 106), module, TWNC2::KICK_PUNCH_PARAM));
        
        addInput(createInputCentered<PJ301MPort>(Vec(20, 143), module, TWNC2::KICK_FREQ_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(60, 143), module, TWNC2::KICK_FM_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(100, 143), module, TWNC2::KICK_PUNCH_CV_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(Vec(20, 184), module, TWNC2::SNARE_VOLUME_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(60, 184), module, TWNC2::SNARE_ENV_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(Vec(100, 184), module, TWNC2::SNARE_NOISE_TONE_PARAM));
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(20, 221), module, TWNC2::SNARE_FREQ_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(60, 221), module, TWNC2::SNARE_NOISE_MIX_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(100, 221), module, TWNC2::SNARE_NOISE_MIX_CV_INPUT));

        addParam(createParamCentered<RoundBlackKnob>(Vec(20, 269), module, TWNC2::HATS_VOLUME_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(60, 269), module, TWNC2::HATS_ENV_INPUT));
        addParam(createParamCentered<RoundBlackKnob>(Vec(100, 269), module, TWNC2::HATS_TONE_PARAM));
        
        addParam(createParamCentered<RoundBlackKnob>(Vec(20, 306), module, TWNC2::HATS_DECAY_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(60, 306), module, TWNC2::HATS_DECAY_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(100, 306), module, TWNC2::EXTERNAL_INPUT));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(22, 343), module, TWNC2::KICK_OUTPUT));
        addParam(createParamCentered<Trimpot>(Vec(26, 368), module, TWNC2::DUCK_PARAM));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(62, 343), module, TWNC2::SNARE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(62, 368), module, TWNC2::MIX_OUTPUT_L));
        
        addOutput(createOutputCentered<PJ301MPort>(Vec(100, 343), module, TWNC2::HATS_OUTPUT1));
        addOutput(createOutputCentered<PJ301MPort>(Vec(100, 368), module, TWNC2::MIX_OUTPUT_R));
    }
};

Model* modelTWNC2 = createModel<TWNC2, TWNC2Widget>("TWNC2");