#include "plugin.hpp"
#include <cmath>
#include <ctime>
#include <cstring>
#include <random>

static constexpr int MAX_BUFFER_SECONDS = 10;
static constexpr int SAMPLE_RATE = 48000;
static constexpr int MAX_BUFFER_SIZE = MAX_BUFFER_SECONDS * SAMPLE_RATE;
static constexpr int MAX_SLICES = 64;
static constexpr int MAX_VOICES = 8;
static constexpr int MAX_MORPHERS = 20;

struct AudioLayer {
    float bufferL[MAX_BUFFER_SIZE];
    float bufferR[MAX_BUFFER_SIZE];
    int playbackPosition = 0;
    float playbackPhase = 0.0f;
    int recordedLength = 0;
    bool active = true;
    int currentSliceIndex = 0;
    int lastScanTargetIndex = -1;
    float fadeEnvelope = 1.0f;
    bool fadingOut = false;
    int pendingSliceIndex = -1;
    int pendingPlaybackPosition = 0;

    AudioLayer() {
        clear();
    }

    void clear() {
        for (int i = 0; i < MAX_BUFFER_SIZE; i++) {
            bufferL[i] = 0.0f;
            bufferR[i] = 0.0f;
        }
        playbackPosition = 0;
        playbackPhase = 0.0f;
        recordedLength = 0;
        currentSliceIndex = 0;
        lastScanTargetIndex = -1;
        fadeEnvelope = 1.0f;
        fadingOut = false;
        pendingSliceIndex = -1;
        pendingPlaybackPosition = 0;
    }
};

// 切片結構
struct Slice {
    int startSample = 0;
    int endSample = 0;
    float peakAmplitude = 0.0f;
    bool active = false;
};

// 參數漸變器
struct ParameterMorpher {
    float originalValue = 0.0f;
    float targetValue = 0.0f;

    enum State {
        IDLE,
        MORPHING,
        HOLDING,
        RETURNING
    };

    enum Curve {
        LINEAR,
        SMOOTH,
        EXPONENTIAL,
        BEZIER
    };

    float interpolate(float a, float b, float t, Curve curve) {
        switch(curve) {
            case LINEAR:
                return a + (b - a) * t;

            case SMOOTH:  // Smoothstep
                t = t * t * (3.0f - 2.0f * t);
                return a + (b - a) * t;

            case EXPONENTIAL:
                t = 1.0f - std::exp(-t * 5.0f);
                return a + (b - a) * t;

            case BEZIER:  // Cubic bezier with control points
                {
                    float u = 1.0f - t;
                    float tt = t * t;
                    float uu = u * u;
                    float ttt = tt * t;
                    float uuu = uu * u;
                    float p1 = 0.3f;
                    float p2 = 0.7f;
                    float result = uuu * a + 3.0f * uu * t * p1 + 3.0f * u * tt * p2 + ttt * b;
                    return result;
                }

            default:
                return a + (b - a) * t;
        }
    }
};

// ============================================================================
// Speed 參數非線性映射
// 旋鈕 0% = -8x, 25% = 0, 50% = 1x, 100% = 8x
// ============================================================================

inline float knobToSpeed(float knob) {
    if (knob < 0.25f) {
        // 0% → -8, 25% → 0
        return -8.0f + knob * 32.0f;
    } else if (knob < 0.5f) {
        // 25% → 0, 50% → 1
        return (knob - 0.25f) * 4.0f;
    } else {
        // 50% → 1, 100% → 8
        return 1.0f + (knob - 0.5f) * 14.0f;
    }
}

inline float speedToKnob(float speed) {
    if (speed < 0.0f) {
        return (speed + 8.0f) / 32.0f;
    } else if (speed < 1.0f) {
        return 0.25f + speed / 4.0f;
    } else {
        return 0.5f + (speed - 1.0f) / 14.0f;
    }
}

// 自定義 ParamQuantity 用於非線性 Speed 顯示
struct SpeedParamQuantity : ParamQuantity {
    float getDisplayValue() override {
        return knobToSpeed(getValue());
    }
    void setDisplayValue(float displayValue) override {
        setValue(speedToKnob(displayValue));
    }
};

// 自定義 ParamQuantity 用於 Poly 參數
struct PolyParamQuantity : ParamQuantity {
    // 使用 defaultValue = 1.0f，基類的 reset() 會自動使用
};

// ============================================================================
// 主模組
// ============================================================================

struct WeiiiDocumenta : Module {

    enum ParamId {
        // 預處理與切片控制
        SCAN_PARAM,              // 手動切片瀏覽 (0-1 映射到 0-numSlices)
        SCAN_CV_ATTEN_PARAM,
        THRESHOLD_PARAM,
        THRESHOLD_CV_ATTEN_PARAM,
        LOOP_END_PARAM,          // Loop 結束點 (0-1 映射到 0-recordedLength)

        // 漸變隨機
        MORPH_BUTTON_PARAM,

        // 回饋
        FEEDBACK_AMOUNT_PARAM,
        FEEDBACK_AMOUNT_CV_ATTEN_PARAM,

        // EQ
        EQ_LOW_PARAM,
        EQ_MID_PARAM,
        EQ_HIGH_PARAM,

        // Speed (播放速度: -1到+1, 中間0為1x, 往右最大8x, 往左反向最快-8x)
        SPEED_PARAM,
        POLY_PARAM,              // Polyphonic voices (1-8)

        // S&H (Sample & Hold)
        SH_SLEW_PARAM,              // S&H Slew time
        SH_AMOUNT_PARAM,            // S&H Output level
        SH_AMOUNT_CV_ATTEN_PARAM,
        SH_RATE_PARAM,              // S&H Sample rate
        SH_RATE_CV_ATTEN_PARAM,

        // 按鈕
        REC_BUTTON_PARAM,
        PLAY_BUTTON_PARAM,
        CLEAR_BUTTON_PARAM,

        PARAMS_LEN
    };

    enum InputId {
        AUDIO_INPUT_L,
        AUDIO_INPUT_R,

        // CV 輸入
        SCAN_CV_INPUT,
        THRESHOLD_CV_INPUT,
        FEEDBACK_AMOUNT_CV_INPUT,
        SPEED_CV_INPUT,
        POLY_CV_INPUT,
        SH_AMOUNT_CV_INPUT,
        SH_RATE_CV_INPUT,

        // Send/Return
        RETURN_L_INPUT,
        RETURN_R_INPUT,

        // Trigger inputs
        REC_TRIGGER_INPUT,
        PLAY_TRIGGER_INPUT,
        CLEAR_TRIGGER_INPUT,
        MORPH_TRIGGER_INPUT,

        INPUTS_LEN
    };

    enum OutputId {
        MAIN_OUTPUT_L,
        MAIN_OUTPUT_R,
        SH_CV_OUTPUT,
        SEND_L_OUTPUT,
        SEND_R_OUTPUT,

        OUTPUTS_LEN
    };

    enum LightId {
        REC_LIGHT,
        ENUMS(PLAY_LIGHT, 2),  // GreenBlue light for Play (green) / Loop (blue)
        MORPH_LIGHT,

        LIGHTS_LEN
    };

    // 資料成員
    AudioLayer layer;
    Slice slices[MAX_SLICES];  // 固定陣列取代 vector
    int numSlices = 0;         // 切片數量計數器

    // 錄音狀態
    bool isRecording = false;
    bool isPlaying = false;
    bool isLooping = false;  // Loop mode
    int recordPosition = 0;

    // Clear button hold timer
    float clearButtonHoldTimer = 0.0f;
    bool clearButtonPressed = false;

    // 切片狀態 (numSlices 由 slices.size() 取代)
    float lastAmplitude = 0.0f;
    int currentSliceIndex = 0;
    float lastThreshold = 1.0f;  // 追蹤上一次的 threshold 值以偵測變化
    float lastMinSliceTime = 0.05f;  // 追蹤上一次的最小切片時間

    // 漸變系統
    ParameterMorpher morphers[MAX_MORPHERS];  // 固定陣列
    ParameterMorpher::State morphState = ParameterMorpher::IDLE;
    float morphProgress = 0.0f;
    float morphTime = 1.0f;  // 漸變時間（秒），可在右鍵選單調整 1-20 秒
    float morphAmount = 1.0f;  // Morph S&H 增益（0-5x），可在右鍵選單調整
    ParameterMorpher::Curve morphCurve = ParameterMorpher::SMOOTH;

    // Morph Target 開關（預設除了 THRSH, min slice time, S&H 三功能以外全開）
    bool morphTargetEqLow = true;
    bool morphTargetEqMid = true;
    bool morphTargetEqHigh = true;
    bool morphTargetThreshold = false;
    bool morphTargetMinSlice = false;
    bool morphTargetScan = true;
    bool morphTargetFeedback = true;
    bool morphTargetShSlew = false;
    bool morphTargetShAmount = false;
    bool morphTargetShRate = false;
    bool morphTargetSpeed = true;

    // 觸發器
    dsp::SchmittTrigger recTrigger;
    dsp::SchmittTrigger playTrigger;
    dsp::SchmittTrigger clearTrigger;

    // EQ 濾波器 (3-band: Low 80Hz, Mid 2.5kHz, High 12kHz)
    dsp::TBiquadFilter<> eqLowL;
    dsp::TBiquadFilter<> eqLowR;
    dsp::TBiquadFilter<> eqMidL;
    dsp::TBiquadFilter<> eqMidR;
    dsp::TBiquadFilter<> eqHighL;
    dsp::TBiquadFilter<> eqHighR;

    // No-Input Feedback (即時回饋，無延遲)
    float lastOutputL = 0.0f;
    float lastOutputR = 0.0f;

    // S&H (Sample & Hold) - Source from EQ'd feedback
    float sampleHoldValue = 0.0f;      // 當前採樣值
    float sampleHoldOutput = 0.0f;     // 經過 slew 後的輸出（±10V）
    float sampleHoldTimer = 0.0f;      // S&H 計時器
    float sampleHoldNormalized = 0.5f; // S&H歸一化值(0-1)
    float sampleHoldCV = 0.0f;         // S&H CV輸出（±10V with AMT）

    // ===== Parameter smoothing to prevent zipper noise =====
    struct SmoothedParam {
        float value = 0.f;
        float target = 0.f;

        void setTarget(float newTarget) {
            target = newTarget;
        }

        float process() {
            // Exponential smoothing with ~5ms time constant at 44.1kHz
            const float alpha = 0.995f;
            value = value * alpha + target * (1.f - alpha);
            return value;
        }

        void reset(float initValue) {
            value = initValue;
            target = initValue;
        }
    };

    // Smoothed parameters for all knobs
    SmoothedParam smoothedScan;
    SmoothedParam smoothedThreshold;
    SmoothedParam smoothedLoopEnd;
    SmoothedParam smoothedFeedbackAmount;
    SmoothedParam smoothedFeedbackDelay;

    // ===== Polyphonic Voice System =====
    // Slice crossfade: 0.1ms fade in/out to prevent clicks (max freq ~5kHz)
    static constexpr float SLICE_FADE_TIME_MS = 0.1f;

    struct Voice {
        int sliceIndex = 0;           // 此 voice 正在播放的 slice
        int playbackPosition = 0;     // 在該 slice 內的播放位置
        float playbackPhase = 0.0f;   // 播放相位（用於速度控制）
        float sliceChangeTimer = 0.0f; // 計時器：用於動態切換 slice
        float speedMultiplier = 1.0f;  // 每個 voice 的隨機速度倍率 (0.5-2.0)
        // Slice crossfade state
        float fadeEnvelope = 1.0f;    // 0-1 fade envelope
        bool fadingOut = false;       // Currently fading out
        int pendingSliceIndex = -1;   // Slice to switch to after fade out
        int pendingPlaybackPosition = 0; // Position to start at after fade out
    };

    Voice voices[MAX_VOICES];         // 固定陣列取代 vector
    int numVoices = 1;                // Voice 數量（1-8）
    std::default_random_engine randomEngine; // 隨機數生成器

    WeiiiDocumenta() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Initialize random engine with time-based seed
        std::time_t t = std::time(nullptr);
        randomEngine.seed(t > 0 ? static_cast<unsigned int>(t) : 12345);

        // 切片控制參數
        configParam(SCAN_PARAM, 0.0f, 1.0f, 0.0f, "Slice Scan", "%", 0.f, 100.f);
        configParam(SCAN_CV_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Scan CV Attenuverter");
        configParam(THRESHOLD_PARAM, 0.0f, 10.0f, 1.0f, "Slice Threshold", " V");
        configParam(THRESHOLD_CV_ATTEN_PARAM, 0.001f, 1.0f, 0.05f, "Min Slice Time", " s");
        configParam(LOOP_END_PARAM, 0.0f, 1.0f, 1.0f, "Loop End Point", "%", 0.f, 100.f);

        configButton(MORPH_BUTTON_PARAM, "Morph Random (Hold)");

        // 回饋參數
        configParam(FEEDBACK_AMOUNT_PARAM, 0.0f, 1.0f, 0.0f, "Feedback Amount");
        configParam(FEEDBACK_AMOUNT_CV_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "Feedback CV Attenuverter");

        // EQ (參考Allen & Heath頻率: Low 80Hz, Mid 2.5kHz, High 12kHz)
        configParam(EQ_LOW_PARAM, -12.0f, 12.0f, 0.0f, "Low EQ (80Hz)", " dB");
        configParam(EQ_MID_PARAM, -12.0f, 12.0f, 0.0f, "Mid EQ (2.5kHz)", " dB");
        configParam(EQ_HIGH_PARAM, -12.0f, 12.0f, 0.0f, "High EQ (12kHz)", " dB");

        // Speed: 旋鈕 0-1 映射到 -8x~8x (25%=0, 50%=1x)
        configParam(SPEED_PARAM, 0.0f, 1.0f, 0.5f, "Playback Speed", "x");
        delete paramQuantities[SPEED_PARAM];
        paramQuantities[SPEED_PARAM] = new SpeedParamQuantity;
        paramQuantities[SPEED_PARAM]->module = this;
        paramQuantities[SPEED_PARAM]->paramId = SPEED_PARAM;
        paramQuantities[SPEED_PARAM]->minValue = 0.0f;
        paramQuantities[SPEED_PARAM]->maxValue = 1.0f;
        paramQuantities[SPEED_PARAM]->defaultValue = 0.5f;  // 1x 速度
        paramQuantities[SPEED_PARAM]->name = "Playback Speed";
        paramQuantities[SPEED_PARAM]->unit = "x";

        // Poly: 1-8 voices, 預設 1
        configParam(POLY_PARAM, 1.0f, 8.0f, 1.0f, "Polyphonic Voices");
        delete paramQuantities[POLY_PARAM];
        paramQuantities[POLY_PARAM] = new PolyParamQuantity;
        paramQuantities[POLY_PARAM]->module = this;
        paramQuantities[POLY_PARAM]->paramId = POLY_PARAM;
        paramQuantities[POLY_PARAM]->minValue = 1.0f;
        paramQuantities[POLY_PARAM]->maxValue = 8.0f;
        paramQuantities[POLY_PARAM]->defaultValue = 1.0f;  // 1 voice
        paramQuantities[POLY_PARAM]->name = "Polyphonic Voices";
        paramQuantities[POLY_PARAM]->snapEnabled = true;

        // S&H (Sample & Hold) - Rate使用對數映射，中央為1Hz, AMT為增益控制(0-5x)
        configParam(SH_SLEW_PARAM, 0.0f, 1.0f, 0.3f, "S&H Slew Time", " s", 0.f, 1.f);
        configParam(SH_AMOUNT_PARAM, 0.0f, 5.0f, 2.0f, "S&H Gain", "x");
        configParam(SH_AMOUNT_CV_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "S&H Gain CV Attenuverter");
        configParam(SH_RATE_PARAM, std::log2(0.01f), std::log2(100.0f), std::log2(1.0f), "S&H Sample Rate", " Hz", 2.f);
        configParam(SH_RATE_CV_ATTEN_PARAM, -1.0f, 1.0f, 0.0f, "S&H Rate CV Attenuverter");

        // 按鈕
        configButton(REC_BUTTON_PARAM, "Record");
        configButton(PLAY_BUTTON_PARAM, "Play/Loop (cycles: Loop → Play)");
        configButton(CLEAR_BUTTON_PARAM, "Stop (hold 2 sec to Clear)");

        // 輸入
        configInput(AUDIO_INPUT_L, "Audio L");
        configInput(AUDIO_INPUT_R, "Audio R");
        configInput(SCAN_CV_INPUT, "Slice Scan CV");
        configInput(THRESHOLD_CV_INPUT, "Threshold CV");
        configInput(FEEDBACK_AMOUNT_CV_INPUT, "Feedback Amount CV");
        configInput(SPEED_CV_INPUT, "Speed CV");
        configInput(POLY_CV_INPUT, "Polyphonic CV");
        configInput(SH_AMOUNT_CV_INPUT, "S&H Amount CV");
        configInput(SH_RATE_CV_INPUT, "S&H Rate CV");
        configInput(RETURN_L_INPUT, "Return L");
        configInput(RETURN_R_INPUT, "Return R");
        configInput(REC_TRIGGER_INPUT, "Rec Trigger");
        configInput(PLAY_TRIGGER_INPUT, "Play Trigger");
        configInput(CLEAR_TRIGGER_INPUT, "Clear Trigger");
        configInput(MORPH_TRIGGER_INPUT, "Morph Gate");

        // 輸出
        configOutput(MAIN_OUTPUT_L, "Main L");
        configOutput(MAIN_OUTPUT_R, "Main R");
        configOutput(SH_CV_OUTPUT, "S&H CV");
        configOutput(SEND_L_OUTPUT, "Send L");
        configOutput(SEND_R_OUTPUT, "Send R");

        // 初始化 feedback 狀態
        lastOutputL = 0.0f;
        lastOutputR = 0.0f;

        // morphers 已是固定陣列，無需 resize

        // 初始化 smoothed parameters
        smoothedScan.reset(0.0f);
        smoothedThreshold.reset(1.0f);
        smoothedLoopEnd.reset(1.0f);
        smoothedFeedbackAmount.reset(0.0f);
        smoothedFeedbackDelay.reset(0.5f);
    }

    // 處理單一樣本（在 oversample 速率下執行）
    std::pair<float, float> processSingleSample(float inputL, float inputR, float sampleRate, float sampleTime) {
        // 播放和混音（在 oversample 速率下執行）
        float outputL = 0.0f;
        float outputR = 0.0f;

        // Calculate fade increment: 0.1ms fade time
        // fadeIncrement = 1.0 / (fadeTime_ms * sampleRate / 1000)
        float fadeIncrement = 1000.0f / (SLICE_FADE_TIME_MS * sampleRate);

        if ((isPlaying || isLooping) && layer.active && layer.recordedLength > 0) {
            // Polyphonic playback - mix all voices
            if (numVoices == 1) {
                // Single voice - use layer playback position

                // Update fade envelope
                if (layer.fadingOut) {
                    layer.fadeEnvelope -= fadeIncrement;
                    if (layer.fadeEnvelope <= 0.0f) {
                        layer.fadeEnvelope = 0.0f;
                        layer.fadingOut = false;
                        // Execute pending slice switch
                        if (layer.pendingSliceIndex >= 0) {
                            layer.currentSliceIndex = layer.pendingSliceIndex;
                            layer.playbackPosition = layer.pendingPlaybackPosition;
                            layer.playbackPhase = 0.0f;
                            layer.pendingSliceIndex = -1;
                        }
                    }
                } else if (layer.fadeEnvelope < 1.0f) {
                    // Fading in
                    layer.fadeEnvelope += fadeIncrement;
                    if (layer.fadeEnvelope > 1.0f) {
                        layer.fadeEnvelope = 1.0f;
                    }
                }

                float floatPos = (float)layer.playbackPosition + layer.playbackPhase;
                int pos0 = (int)floatPos % layer.recordedLength;
                int pos1 = (pos0 + 1) % layer.recordedLength;
                float frac = floatPos - (int)floatPos;

                // 線性插值
                outputL = layer.bufferL[pos0] * (1.0f - frac) + layer.bufferL[pos1] * frac;
                outputR = layer.bufferR[pos0] * (1.0f - frac) + layer.bufferR[pos1] * frac;

                // Apply fade envelope
                outputL *= layer.fadeEnvelope;
                outputR *= layer.fadeEnvelope;
            } else {
                // Multiple voices - mix them together
                for (int i = 0; i < numVoices; i++) {
                    // Update fade envelope for this voice
                    if (voices[i].fadingOut) {
                        voices[i].fadeEnvelope -= fadeIncrement;
                        if (voices[i].fadeEnvelope <= 0.0f) {
                            voices[i].fadeEnvelope = 0.0f;
                            voices[i].fadingOut = false;
                            // Execute pending slice switch
                            if (voices[i].pendingSliceIndex >= 0) {
                                voices[i].sliceIndex = voices[i].pendingSliceIndex;
                                voices[i].playbackPosition = voices[i].pendingPlaybackPosition;
                                voices[i].playbackPhase = 0.0f;
                                voices[i].pendingSliceIndex = -1;
                            }
                        }
                    } else if (voices[i].fadeEnvelope < 1.0f) {
                        // Fading in
                        voices[i].fadeEnvelope += fadeIncrement;
                        if (voices[i].fadeEnvelope > 1.0f) {
                            voices[i].fadeEnvelope = 1.0f;
                        }
                    }

                    float floatPos = (float)voices[i].playbackPosition + voices[i].playbackPhase;
                    int pos0 = (int)floatPos % layer.recordedLength;
                    int pos1 = (pos0 + 1) % layer.recordedLength;
                    float frac = floatPos - (int)floatPos;

                    // 線性插值 with fade envelope
                    float voiceL = (layer.bufferL[pos0] * (1.0f - frac) + layer.bufferL[pos1] * frac) * voices[i].fadeEnvelope;
                    float voiceR = (layer.bufferR[pos0] * (1.0f - frac) + layer.bufferR[pos1] * frac) * voices[i].fadeEnvelope;
                    outputL += voiceL;
                    outputR += voiceR;
                }

                // Divide by numVoices to prevent clipping
                outputL /= (float)numVoices;
                outputR /= (float)numVoices;
            }
        }

        // ===== No-Input Feedback 回饋處理 =====
        float feedbackAmount = smoothedFeedbackAmount.process();
        if (feedbackAmount > 0.0f) {
            // Analog-style soft saturation with tanh
            // Scale factor 0.3 keeps small signals linear while saturating large ones
            float fbL = std::tanh(lastOutputL * 0.3f) / 0.3f;
            float fbR = std::tanh(lastOutputR * 0.3f) / 0.3f;

            outputL += fbL * feedbackAmount;
            outputR += fbR * feedbackAmount;
        }

        return {clamp(outputL, -10.0f, 10.0f), clamp(outputR, -10.0f, 10.0f)};
    }

    void process(const ProcessArgs& args) override {
        // ===== 更新 smoothed parameter 目標值 =====
        smoothedScan.setTarget(params[SCAN_PARAM].getValue());

        // Threshold with CV
        float thresholdValue = params[THRESHOLD_PARAM].getValue();
        if (inputs[THRESHOLD_CV_INPUT].isConnected()) {
            float thresholdCv = inputs[THRESHOLD_CV_INPUT].getVoltage();
            thresholdValue = clamp(thresholdValue + thresholdCv, 0.0f, 10.0f);
        }
        smoothedThreshold.setTarget(thresholdValue);

        smoothedLoopEnd.setTarget(params[LOOP_END_PARAM].getValue());

        // Feedback Amount with CV
        float feedbackValue = params[FEEDBACK_AMOUNT_PARAM].getValue();
        if (inputs[FEEDBACK_AMOUNT_CV_INPUT].isConnected()) {
            float feedbackCv = inputs[FEEDBACK_AMOUNT_CV_INPUT].getVoltage() / 10.0f; // 0-10V -> 0-1
            float feedbackAtten = params[FEEDBACK_AMOUNT_CV_ATTEN_PARAM].getValue();
            feedbackValue = clamp(feedbackValue + feedbackCv * feedbackAtten, 0.0f, 1.0f);
        }
        smoothedFeedbackAmount.setTarget(feedbackValue);

        // 處理按鈕觸發（在每個原始樣本執行一次）
        float recTriggerSignal = params[REC_BUTTON_PARAM].getValue();
        if (inputs[REC_TRIGGER_INPUT].isConnected()) {
            recTriggerSignal += inputs[REC_TRIGGER_INPUT].getVoltage();
        }
        if (recTrigger.process(recTriggerSignal)) {
            isRecording = !isRecording;
            if (isRecording) {
                recordPosition = 0;
                numSlices = 0;  // 重置切片
                lastAmplitude = 0.0f;
                lastThreshold = smoothedThreshold.value;  // 記錄當前 threshold
            } else {
                // 錄音停止：記錄實際長度並結束最後一個切片
                layer.recordedLength = recordPosition;
                if (numSlices > 0 && slices[numSlices - 1].active) {
                    slices[numSlices - 1].endSample = recordPosition;
                }
            }
        }

        // PLAY/LOOP button: toggles between Loop ↔ Play
        float playTriggerSignal = params[PLAY_BUTTON_PARAM].getValue();
        if (inputs[PLAY_TRIGGER_INPUT].isConnected()) {
            playTriggerSignal += inputs[PLAY_TRIGGER_INPUT].getVoltage();
        }
        if (playTrigger.process(playTriggerSignal)) {
            if (isLooping) {
                // Loop → Play
                isLooping = false;
                isPlaying = true;
            } else {
                // Play → Loop (or stopped → Loop)
                isLooping = true;
                isPlaying = false;
            }
        }

        // ===== Polyphonic voice management =====
        float polyValue = params[POLY_PARAM].getValue();
        if (inputs[POLY_CV_INPUT].isConnected()) {
            float polyCv = inputs[POLY_CV_INPUT].getVoltage() / 10.0f * 7.0f; // 0-10V -> 0-7 additional voices
            polyValue = clamp(polyValue + polyCv, 1.0f, 8.0f);
        }
        int newNumVoices = (int)std::round(polyValue);
        newNumVoices = clamp(newNumVoices, 1, 8);

        if (newNumVoices != numVoices) {
            numVoices = newNumVoices;
            // voices 已是固定陣列，無需 resize

            // Initialize voices with random slice selection
            if (numSlices > 0 && numVoices > 1) {
                std::uniform_int_distribution<int> sliceDist(0, numSlices - 1);
                std::uniform_real_distribution<float> speedDist(0.5f, 2.0f);

                for (int i = 0; i < numVoices; i++) {
                    // Voice 0: use current layer slice, no speed variation
                    if (i == 0) {
                        voices[i].sliceIndex = layer.currentSliceIndex;
                        voices[i].playbackPosition = layer.playbackPosition;
                        voices[i].playbackPhase = layer.playbackPhase;
                        voices[i].speedMultiplier = 1.0f;
                    } else {
                        // Other voices: random slice selection and random speed
                        int targetSliceIndex = sliceDist(randomEngine);
                        voices[i].sliceIndex = targetSliceIndex;
                        voices[i].playbackPosition = slices[targetSliceIndex].startSample;
                        voices[i].playbackPhase = 0.0f;
                        voices[i].speedMultiplier = speedDist(randomEngine);
                    }

                    // Random initial timer for slice changes (0.5-2.0 seconds)
                    std::uniform_real_distribution<float> timerDist(0.5f, 2.0f);
                    voices[i].sliceChangeTimer = timerDist(randomEngine);
                }
            } else {
                // Single voice or no slices
                for (int i = 0; i < numVoices; i++) {
                    voices[i].sliceIndex = 0;
                    voices[i].playbackPosition = 0;
                    voices[i].playbackPhase = 0.0f;
                    voices[i].sliceChangeTimer = 0.0f;
                    voices[i].speedMultiplier = 1.0f;
                }
            }
        }

        // STOP/CLEAR button: short press = stop, hold 2 sec = clear
        float clearTriggerSignal = params[CLEAR_BUTTON_PARAM].getValue();
        if (inputs[CLEAR_TRIGGER_INPUT].isConnected()) {
            clearTriggerSignal += inputs[CLEAR_TRIGGER_INPUT].getVoltage();
        }

        bool buttonIsPressed = (clearTriggerSignal > 0.5f);

        if (buttonIsPressed) {
            if (!clearButtonPressed) {
                // Button just pressed - stop playback immediately
                isPlaying = false;
                isLooping = false;
                clearButtonPressed = true;
                clearButtonHoldTimer = 0.0f;
            } else {
                // Button held - increment timer
                clearButtonHoldTimer += args.sampleTime;

                // Clear after 2 seconds
                if (clearButtonHoldTimer >= 2.0f) {
                    layer.clear();
                    recordPosition = 0;
                    numSlices = 0;  // 重置切片
                    clearButtonHoldTimer = 0.0f;  // Reset to prevent repeated clearing
                }
            }
        } else {
            // Button released
            clearButtonPressed = false;
            clearButtonHoldTimer = 0.0f;
        }

        // ===== 自動偵測 threshold 或 min slice time 變化並重新掃描切片 =====
        // 先 process threshold 以保持值更新（無論是否錄音）
        float currentThreshold = smoothedThreshold.process();
        float currentMinSliceTime = params[THRESHOLD_CV_ATTEN_PARAM].getValue();

        // 不在錄音時才進行重新掃描
        if (!isRecording) {
            bool thresholdChanged = std::abs(currentThreshold - lastThreshold) > 0.05f;
            bool minTimeChanged = std::abs(currentMinSliceTime - lastMinSliceTime) > 0.001f;

            if (thresholdChanged || minTimeChanged) {
                rescanSlices();
                lastThreshold = currentThreshold;
                lastMinSliceTime = currentMinSliceTime;
            }
        }

        // 更新燈號
        lights[REC_LIGHT].setBrightness(isRecording ? 1.0f : 0.0f);

        // PLAY/LOOP light: GreenBlueLight - Green for Play, Blue for Loop
        if (isPlaying) {
            // Green for Play mode
            lights[PLAY_LIGHT + 0].setBrightness(1.0f);  // Green
            lights[PLAY_LIGHT + 1].setBrightness(0.0f);  // Blue
        } else if (isLooping) {
            // Blue for Loop mode
            lights[PLAY_LIGHT + 0].setBrightness(0.0f);  // Green
            lights[PLAY_LIGHT + 1].setBrightness(1.0f);  // Blue
        } else {
            // Off for Stop
            lights[PLAY_LIGHT + 0].setBrightness(0.0f);  // Green
            lights[PLAY_LIGHT + 1].setBrightness(0.0f);  // Blue
        }

        // 處理漸變隨機系統
        processMorphing(args);

        // 讀取音訊輸入
        float inputL = inputs[AUDIO_INPUT_L].getVoltage();
        float inputR = inputs[AUDIO_INPUT_R].isConnected() ?
            inputs[AUDIO_INPUT_R].getVoltage() : inputL;

        // 錄音（在原始速率執行，不進行 oversample）
        if (isRecording) {
            if (recordPosition < MAX_BUFFER_SIZE) {
                layer.bufferL[recordPosition] = inputL;
                layer.bufferR[recordPosition] = inputR;

                // 即時更新錄音長度
                layer.recordedLength = recordPosition + 1;

                // 切片檢測：偵測音量突變 使用混合訊號
                float threshold = smoothedThreshold.value;
                float mixedSample = (inputL + inputR) * 0.5f;
                float currentAmp = std::abs(mixedSample);

                // 偵測從低音量到高音量的突變（attack）
                if (lastAmplitude < threshold && currentAmp >= threshold) {
                    // 結束上一個切片
                    if (numSlices > 0 && slices[numSlices - 1].active) {
                        slices[numSlices - 1].endSample = recordPosition - 1;
                    }

                    // 開始新切片
                    if (numSlices < MAX_SLICES) {
                        Slice newSlice;
                        newSlice.startSample = recordPosition;
                        newSlice.active = true;
                        newSlice.peakAmplitude = 0.0f;
                        slices[numSlices++] = newSlice;
                    }
                }

                // 更新當前切片的 peak amplitude
                if (numSlices > 0 && slices[numSlices - 1].active) {
                    slices[numSlices - 1].peakAmplitude = std::max(
                        slices[numSlices - 1].peakAmplitude, currentAmp);
                }

                lastAmplitude = currentAmp;

                recordPosition++;
            }
        }

        // 直接處理（移除 oversampling 以改善音質）
        auto [outputL, outputR] = processSingleSample(inputL, inputR, args.sampleRate, args.sampleTime);

        // 播放位置推進（每個原始樣本一次）
        if (isPlaying || isLooping) {
            float scanValue = smoothedScan.process();

            // 處理 SCAN CV 輸入
            if (inputs[SCAN_CV_INPUT].isConnected()) {
                float cv = inputs[SCAN_CV_INPUT].getVoltage() / 10.0f;  // 0-10V -> 0-1
                float atten = params[SCAN_CV_ATTEN_PARAM].getValue();
                scanValue = clamp(scanValue + cv * atten, 0.0f, 1.0f);
            }

            // 處理 S&H 內建調變 Scan
            // 使用上一個 sample 的 S&H 值（一個 sample 延遲可接受）
            float shGain = params[SH_AMOUNT_PARAM].getValue();
            if (shGain > 0.01f && std::abs(sampleHoldCV) > 0.001f) {
                // sampleHoldCV 已經包含增益（±10V * gain，clamp 到 ±10V）
                // 轉換為 0-1 用於 scan：(±10V + 10V) / 20V = 0-1
                float shForScan = (sampleHoldCV + 10.0f) / 20.0f;
                scanValue = clamp(scanValue + shForScan, 0.0f, 1.0f);
            }

            float loopEnd = smoothedLoopEnd.process();

            if (layer.active && layer.recordedLength > 0) {
                // 計算 loop 結束點
                int loopEndSample = (int)(loopEnd * layer.recordedLength);
                loopEndSample = clamp(loopEndSample, 1, layer.recordedLength);

                if (numSlices > 1) {
                    // SCAN 功能：手動選擇切片（包含 Scan 參數、CV 輸入、或 S&H 調變）
                    bool useManualScan = (scanValue > 0.01f) ||
                                        (inputs[SCAN_CV_INPUT].isConnected() &&
                                         std::abs(params[SCAN_CV_ATTEN_PARAM].getValue()) > 0.01f) ||
                                        (shGain > 0.01f);

                    if (useManualScan) {
                        int targetSliceIndex = (int)std::round(scanValue * (numSlices - 1));
                        targetSliceIndex = clamp(targetSliceIndex, 0, numSlices - 1);

                        // 只在 SCAN 目標切片改變時才跳轉（使用 crossfade）
                        if (targetSliceIndex != layer.lastScanTargetIndex && slices[targetSliceIndex].active) {
                            layer.lastScanTargetIndex = targetSliceIndex;

                            // Use crossfade for slice switching
                            if (numVoices == 1) {
                                // Single voice mode - trigger fade out with pending switch
                                if (!layer.fadingOut && layer.pendingSliceIndex < 0) {
                                    layer.fadingOut = true;
                                    layer.pendingSliceIndex = targetSliceIndex;
                                    layer.pendingPlaybackPosition = slices[targetSliceIndex].startSample;
                                }
                            } else {
                                // Polyphonic mode - sync voice 0 with crossfade
                                if (!voices[0].fadingOut && voices[0].pendingSliceIndex < 0) {
                                    voices[0].fadingOut = true;
                                    voices[0].pendingSliceIndex = targetSliceIndex;
                                    voices[0].pendingPlaybackPosition = slices[targetSliceIndex].startSample;
                                }
                            }
                        }
                    } else {
                        // 不使用 SCAN 時，重設追蹤變數
                        layer.lastScanTargetIndex = -1;
                    }
                }

                // 播放速度控制: 旋鈕非線性映射 -8x 到 +8x
                float playbackSpeed = knobToSpeed(params[SPEED_PARAM].getValue());
                if (inputs[SPEED_CV_INPUT].isConnected()) {
                    float speedCv = inputs[SPEED_CV_INPUT].getVoltage();
                    playbackSpeed = clamp(playbackSpeed + speedCv, -8.0f, 8.0f);
                }

                // 檢查是否超出範圍（支援正反向播放）
                bool isReverse = playbackSpeed < 0.0f;

                if (numVoices == 1) {
                    // Single voice mode - update layer playback position
                    // 累積播放相位以支援慢速播放
                    layer.playbackPhase += playbackSpeed;

                    // 當累積超過1.0時，前進playback位置
                    int positionDelta = (int)layer.playbackPhase;
                    layer.playbackPhase -= (float)positionDelta;
                    layer.playbackPosition += positionDelta;

                    if (numSlices > 0 && layer.currentSliceIndex < numSlices) {
                        if (slices[layer.currentSliceIndex].active) {
                            int sliceStart = slices[layer.currentSliceIndex].startSample;
                            int sliceEnd = slices[layer.currentSliceIndex].endSample;

                            if (isReverse) {
                                // 反向播放：檢查是否小於起點，前進到上一個切片
                                if (layer.playbackPosition < sliceStart) {
                                    // Use crossfade for slice boundary
                                    int newSliceIndex = (layer.currentSliceIndex > 0) ? layer.currentSliceIndex - 1 : numSlices - 1;
                                    int newPosition = (layer.currentSliceIndex > 0) ? slices[newSliceIndex].endSample : loopEndSample - 1;
                                    if (!layer.fadingOut && layer.pendingSliceIndex < 0) {
                                        layer.fadingOut = true;
                                        layer.pendingSliceIndex = newSliceIndex;
                                        layer.pendingPlaybackPosition = newPosition;
                                    }
                                }
                            } else {
                                // 正向播放：檢查是否超過終點
                                if (isLooping) {
                                    // Loop mode: loop within current slice only (with crossfade)
                                    if (layer.playbackPosition > sliceEnd) {
                                        if (!layer.fadingOut && layer.pendingSliceIndex < 0) {
                                            layer.fadingOut = true;
                                            layer.pendingSliceIndex = layer.currentSliceIndex;
                                            layer.pendingPlaybackPosition = sliceStart;
                                        }
                                    }
                                } else {
                                    // Play mode: advance to next slice, or loop at end
                                    if (layer.playbackPosition >= loopEndSample) {
                                        // Reached end - loop from beginning (with crossfade)
                                        if (!layer.fadingOut && layer.pendingSliceIndex < 0) {
                                            layer.fadingOut = true;
                                            layer.pendingSliceIndex = 0;
                                            layer.pendingPlaybackPosition = 0;
                                        }
                                    } else if (layer.playbackPosition > sliceEnd) {
                                        // Move to next slice (with crossfade)
                                        int newSliceIndex = (layer.currentSliceIndex + 1) % numSlices;
                                        if (slices[newSliceIndex].active && !layer.fadingOut && layer.pendingSliceIndex < 0) {
                                            layer.fadingOut = true;
                                            layer.pendingSliceIndex = newSliceIndex;
                                            layer.pendingPlaybackPosition = slices[newSliceIndex].startSample;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        // 沒有切片時循環（Loop 和 Play 模式行為相同）
                        if (isReverse) {
                            if (layer.playbackPosition < 0) {
                                layer.playbackPosition = loopEndSample - 1;
                            }
                        } else {
                            if (layer.playbackPosition >= loopEndSample) {
                                layer.playbackPosition = 0;
                            }
                        }
                    }
                } else {
                    // Multiple voices mode - update each voice independently
                    for (int i = 0; i < numVoices; i++) {
                        // Apply per-voice speed multiplier
                        float voiceSpeed = playbackSpeed * voices[i].speedMultiplier;
                        voices[i].playbackPhase += voiceSpeed;

                        int positionDelta = (int)voices[i].playbackPhase;
                        voices[i].playbackPhase -= (float)positionDelta;
                        voices[i].playbackPosition += positionDelta;

                        if (numSlices > 0 && voices[i].sliceIndex < numSlices) {
                            if (slices[voices[i].sliceIndex].active) {
                                int sliceStart = slices[voices[i].sliceIndex].startSample;
                                int sliceEnd = slices[voices[i].sliceIndex].endSample;

                                if (isReverse) {
                                    if (voices[i].playbackPosition < sliceStart) {
                                        // Use crossfade for slice boundary
                                        int newSliceIndex = (voices[i].sliceIndex > 0) ? voices[i].sliceIndex - 1 : numSlices - 1;
                                        int newPosition = (voices[i].sliceIndex > 0) ? slices[newSliceIndex].endSample : loopEndSample - 1;
                                        if (!voices[i].fadingOut && voices[i].pendingSliceIndex < 0) {
                                            voices[i].fadingOut = true;
                                            voices[i].pendingSliceIndex = newSliceIndex;
                                            voices[i].pendingPlaybackPosition = newPosition;
                                        }
                                    }
                                } else {
                                    if (isLooping) {
                                        // Loop mode: loop within current slice only (with crossfade)
                                        if (voices[i].playbackPosition > sliceEnd) {
                                            if (!voices[i].fadingOut && voices[i].pendingSliceIndex < 0) {
                                                voices[i].fadingOut = true;
                                                voices[i].pendingSliceIndex = voices[i].sliceIndex;
                                                voices[i].pendingPlaybackPosition = sliceStart;
                                            }
                                        }
                                    } else {
                                        // Play mode: advance to next slice, or loop at end
                                        if (voices[i].playbackPosition >= loopEndSample) {
                                            // Reached end - loop from beginning (with crossfade)
                                            if (!voices[i].fadingOut && voices[i].pendingSliceIndex < 0) {
                                                voices[i].fadingOut = true;
                                                voices[i].pendingSliceIndex = 0;
                                                voices[i].pendingPlaybackPosition = 0;
                                            }
                                        } else if (voices[i].playbackPosition > sliceEnd) {
                                            // Move to next slice (with crossfade)
                                            int newSliceIndex = (voices[i].sliceIndex + 1) % numSlices;
                                            if (slices[newSliceIndex].active && !voices[i].fadingOut && voices[i].pendingSliceIndex < 0) {
                                                voices[i].fadingOut = true;
                                                voices[i].pendingSliceIndex = newSliceIndex;
                                                voices[i].pendingPlaybackPosition = slices[newSliceIndex].startSample;
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                            // 沒有切片時循環（Loop 和 Play 模式行為相同）
                            if (isReverse) {
                                if (voices[i].playbackPosition < 0) {
                                    voices[i].playbackPosition = loopEndSample - 1;
                                }
                            } else {
                                if (voices[i].playbackPosition >= loopEndSample) {
                                    voices[i].playbackPosition = 0;
                                }
                            }
                        }

                        // ===== Dynamic slice switching for voices (except voice 0) =====
                        if (i > 0 && numSlices > 0) {
                            voices[i].sliceChangeTimer -= args.sampleTime;

                            if (voices[i].sliceChangeTimer <= 0.0f) {
                                // Time to switch to a random slice (with crossfade)
                                std::uniform_int_distribution<int> sliceDist(0, numSlices - 1);
                                int newSliceIndex = sliceDist(randomEngine);

                                // Avoid selecting the same slice
                                int attempts = 0;
                                while (newSliceIndex == voices[i].sliceIndex && numSlices > 1 && attempts < 10) {
                                    newSliceIndex = sliceDist(randomEngine);
                                    attempts++;
                                }

                                // Use crossfade for dynamic slice switch
                                if (!voices[i].fadingOut && voices[i].pendingSliceIndex < 0) {
                                    voices[i].fadingOut = true;
                                    voices[i].pendingSliceIndex = newSliceIndex;
                                    voices[i].pendingPlaybackPosition = slices[newSliceIndex].startSample;
                                }

                                // Reset timer with random interval (0.5-2.0 seconds)
                                std::uniform_real_distribution<float> timerDist(0.5f, 2.0f);
                                voices[i].sliceChangeTimer = timerDist(randomEngine);
                            }
                        }
                    }

                    // Update layer position to voice 0 for UI display
                    if (numVoices > 0) {
                        layer.playbackPosition = voices[0].playbackPosition;
                        layer.playbackPhase = voices[0].playbackPhase;
                        layer.currentSliceIndex = voices[0].sliceIndex;
                    }
                }
            }
        }

        // ===== Send/Return 處理 (在 EQ 前，feedback 迴路中) =====
        // Send 輸出 feedback 訊號
        outputs[SEND_L_OUTPUT].setVoltage(outputL);
        outputs[SEND_R_OUTPUT].setVoltage(outputR);

        // Return 輸入取代訊號（如果有接線）
        if (inputs[RETURN_L_INPUT].isConnected()) {
            outputL = inputs[RETURN_L_INPUT].getVoltage();
        }
        if (inputs[RETURN_R_INPUT].isConnected()) {
            outputR = inputs[RETURN_R_INPUT].getVoltage();
        }

        // EQ處理 (處理混合後的輸出包含feedback)
        float sampleRate = args.sampleRate;
        float eqLowGain = params[EQ_LOW_PARAM].getValue();
        float eqMidGain = params[EQ_MID_PARAM].getValue();
        float eqHighGain = params[EQ_HIGH_PARAM].getValue();

        // Low shelf 80Hz
        eqLowL.setParameters(dsp::TBiquadFilter<>::LOWSHELF, 80.0f / sampleRate, 0.707f, std::pow(10.0f, eqLowGain / 20.0f));
        eqLowR.setParameters(dsp::TBiquadFilter<>::LOWSHELF, 80.0f / sampleRate, 0.707f, std::pow(10.0f, eqLowGain / 20.0f));

        // Mid peaking 2.5kHz
        eqMidL.setParameters(dsp::TBiquadFilter<>::PEAK, 2500.0f / sampleRate, 0.707f, std::pow(10.0f, eqMidGain / 20.0f));
        eqMidR.setParameters(dsp::TBiquadFilter<>::PEAK, 2500.0f / sampleRate, 0.707f, std::pow(10.0f, eqMidGain / 20.0f));

        // High shelf 12kHz
        eqHighL.setParameters(dsp::TBiquadFilter<>::HIGHSHELF, 12000.0f / sampleRate, 0.707f, std::pow(10.0f, eqHighGain / 20.0f));
        eqHighR.setParameters(dsp::TBiquadFilter<>::HIGHSHELF, 12000.0f / sampleRate, 0.707f, std::pow(10.0f, eqHighGain / 20.0f));

        outputL = eqLowL.process(outputL);
        outputL = eqMidL.process(outputL);
        outputL = eqHighL.process(outputL);

        outputR = eqLowR.process(outputR);
        outputR = eqMidR.process(outputR);
        outputR = eqHighR.process(outputR);

        // 儲存 EQ 後的輸出作為下一次的 feedback 來源
        // 這樣 feedback 會包含 Send/Return 和 EQ 的效果
        lastOutputL = outputL;
        lastOutputR = outputR;

        // ===== S&H (Sample & Hold) Processing =====
        // Source: EQ'd feedback audio (use max of L and R to preserve amplitude)
        float feedbackSource = std::max(std::abs(outputL), std::abs(outputR));
        if (outputL < 0.0f && outputR < 0.0f) {
            feedbackSource = -feedbackSource;  // Preserve negative polarity
        }

        // Get S&H Rate parameter with CV modulation
        // SH_RATE_PARAM is in log2 scale, need to convert back
        float shRateLog = params[SH_RATE_PARAM].getValue();
        if (inputs[SH_RATE_CV_INPUT].isConnected()) {
            float rateCv = inputs[SH_RATE_CV_INPUT].getVoltage();  // 0-10V
            float rateAtten = params[SH_RATE_CV_ATTEN_PARAM].getValue();
            shRateLog = clamp(shRateLog + rateCv * rateAtten, std::log2(0.01f), std::log2(100.0f));
        }
        float shRate = std::pow(2.f, shRateLog);  // Convert from log2 to Hz

        // Update S&H timer
        sampleHoldTimer += args.sampleTime;

        // Sample when timer exceeds 1/rate
        float samplePeriod = 1.0f / shRate;
        if (sampleHoldTimer >= samplePeriod) {
            sampleHoldTimer -= samplePeriod;
            // Sample the feedback source
            sampleHoldValue = feedbackSource;
        }

        // Apply exponential slew to smooth the output
        float slewTime = params[SH_SLEW_PARAM].getValue();  // 0-1 seconds
        float tau = slewTime;
        float alpha = 1.0f;
        if (tau > 0.0001f) {
            alpha = 1.0f - std::exp(-args.sampleTime / tau);
        }
        sampleHoldOutput += alpha * (sampleHoldValue - sampleHoldOutput);

        // Convert to bipolar ±10V output
        float bipolarOutput = clamp(sampleHoldOutput, -10.0f, 10.0f);

        // Get S&H Gain parameter with CV modulation (0-5x gain)
        float shGain = params[SH_AMOUNT_PARAM].getValue();
        if (inputs[SH_AMOUNT_CV_INPUT].isConnected()) {
            float gainCv = inputs[SH_AMOUNT_CV_INPUT].getVoltage() * 0.5f;  // 0-10V -> 0-5x
            float gainAtten = params[SH_AMOUNT_CV_ATTEN_PARAM].getValue();
            shGain = clamp(shGain + gainCv * gainAtten, 0.0f, 5.0f);
        }

        // Apply gain to bipolar output (±10V range)
        // Clamp to ±10V to prevent exceeding VCV Rack standard range
        sampleHoldCV = clamp(bipolarOutput * shGain, -10.0f, 10.0f);

        // Calculate normalized value (0-1) for internal use
        sampleHoldNormalized = (bipolarOutput + 10.0f) / 20.0f;  // -10V~+10V -> 0~1

        // Output S&H CV
        outputs[SH_CV_OUTPUT].setVoltage(sampleHoldCV);

        // Soft Limiter at -3dB (7.07V for 10V peak)
        // -3dB = 10V * 10^(-3/20) = 7.07V
        const float limiterThreshold = 7.07f;
        const float saturationAmount = 0.2f;  // 允許少量的 saturation

        auto softLimit = [&](float input) {
            float absInput = std::abs(input);
            float sign = (input >= 0.0f) ? 1.0f : -1.0f;

            if (absInput <= limiterThreshold) {
                return input;
            } else {
                // Soft knee compression above threshold
                float excess = absInput - limiterThreshold;
                float compressed = limiterThreshold + excess / (1.0f + excess * saturationAmount);
                return sign * compressed;
            }
        };

        outputL = softLimit(outputL);
        outputR = softLimit(outputR);

        // 輸出
        outputs[MAIN_OUTPUT_L].setVoltage(outputL);
        outputs[MAIN_OUTPUT_R].setVoltage(outputR);

    }

    void processMorphing(const ProcessArgs& args) {
        bool buttonPressed = params[MORPH_BUTTON_PARAM].getValue() > 0.5f;
        bool gateHigh = inputs[MORPH_TRIGGER_INPUT].getVoltage() >= 1.0f;
        bool morphActive = buttonPressed || gateHigh;
        // 使用成員變數 morphTime 由右鍵選單設定

        morphCurve = ParameterMorpher::SMOOTH;  // Default curve

        // 狀態機
        if (morphActive && morphState == ParameterMorpher::IDLE) {
            // 開始漸變：保存當前值，生成隨機目標
            saveParametersForMorph();
            generateRandomTargets();
            morphState = ParameterMorpher::MORPHING;
            morphProgress = 0.0f;
        }

        if (morphState == ParameterMorpher::MORPHING) {
            if (morphTime > 0.0f) {
                morphProgress += args.sampleTime / morphTime;
            } else {
                morphProgress = 1.0f;  // 如果時間是 0，立即到達
            }

            if (morphProgress >= 1.0f) {
                morphProgress = 1.0f;
                morphState = ParameterMorpher::HOLDING;
            }

            applyMorphing(morphProgress);
        }

        if (morphState == ParameterMorpher::HOLDING) {
            // 維持在隨機位置
            applyMorphing(1.0f);
        }

        if (!morphActive && (morphState == ParameterMorpher::MORPHING ||
                             morphState == ParameterMorpher::HOLDING)) {
            // 放開按鈕：開始回到原始值
            morphState = ParameterMorpher::RETURNING;
            // morphProgress 保持當前值，從這裡開始回歸
        }

        if (morphState == ParameterMorpher::RETURNING) {
            if (morphTime > 0.0f) {
                morphProgress -= args.sampleTime / morphTime;
            } else {
                morphProgress = 0.0f;  // 如果時間是 0，立即回歸
            }

            if (morphProgress <= 0.0f) {
                morphProgress = 0.0f;
                morphState = ParameterMorpher::IDLE;
                restoreOriginalParameters();
            } else {
                applyMorphing(morphProgress);
            }
        }

        // 更新漸變燈號
        lights[MORPH_LIGHT].setBrightness(
            (morphState != ParameterMorpher::IDLE) ? morphProgress : 0.0f
        );
    }

    void saveParametersForMorph() {
        // 保存可漸變的參數（根據 morphTarget 開關）
        int idx = 0;
        if (morphTargetEqLow) morphers[idx++].originalValue = params[EQ_LOW_PARAM].getValue();
        if (morphTargetEqMid) morphers[idx++].originalValue = params[EQ_MID_PARAM].getValue();
        if (morphTargetEqHigh) morphers[idx++].originalValue = params[EQ_HIGH_PARAM].getValue();
        if (morphTargetThreshold) morphers[idx++].originalValue = params[THRESHOLD_PARAM].getValue();
        if (morphTargetMinSlice) morphers[idx++].originalValue = params[THRESHOLD_CV_ATTEN_PARAM].getValue();
        if (morphTargetScan) morphers[idx++].originalValue = params[SCAN_PARAM].getValue();
        if (morphTargetFeedback) morphers[idx++].originalValue = params[FEEDBACK_AMOUNT_PARAM].getValue();
        if (morphTargetShSlew) morphers[idx++].originalValue = params[SH_SLEW_PARAM].getValue();
        if (morphTargetShAmount) morphers[idx++].originalValue = params[SH_AMOUNT_PARAM].getValue();
        if (morphTargetShRate) morphers[idx++].originalValue = params[SH_RATE_PARAM].getValue();
        if (morphTargetSpeed) morphers[idx++].originalValue = params[SPEED_PARAM].getValue();
    }

    void generateRandomTargets() {
        // 使用 S&H 訊號（未增益）乘以 morphAmount
        float shBase = sampleHoldNormalized;  // 0-1 範圍
        // morphAmount 0-5x，控制變化強度

        int idx = 0;

        // 策略：每個參數 = (S&H值 + 該參數的random值) × morphAmount × baseRange
        // S&H值從 0-1 映射到 -1 ~ +1
        float shDirection = (shBase - 0.5f) * 2.0f;  // -1 到 +1

        // morphAmount 範圍 0-5
        float morphScale = morphAmount;  // 直接使用 morphAmount (0-5)

        if (morphTargetEqLow) {
            float randomDir = random::uniform() * 2.0f - 1.0f;  // ±1
            float combinedDirection = shDirection + randomDir;  // 範圍 -2 到 +2
            float current = morphers[idx].originalValue;
            float baseRange = 12.0f;  // 單邊全範圍 12dB
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, -12.0f, 12.0f);
        }
        if (morphTargetEqMid) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 12.0f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, -12.0f, 12.0f);
        }
        if (morphTargetEqHigh) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 12.0f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, -12.0f, 12.0f);
        }
        if (morphTargetThreshold) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 5.0f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, 0.0f, 10.0f);
        }
        if (morphTargetMinSlice) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 0.5f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, 0.001f, 1.0f);
        }
        if (morphTargetScan) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 0.5f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, 0.0f, 1.0f);
        }
        if (morphTargetFeedback) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 0.5f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, 0.0f, 1.0f);
        }
        if (morphTargetShSlew) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 0.5f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, 0.0f, 1.0f);
        }
        if (morphTargetShAmount) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 2.5f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, 0.0f, 5.0f);
        }
        if (morphTargetShRate) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 6.644f;
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, std::log2(0.01f), std::log2(100.0f));
        }
        if (morphTargetSpeed) {
            float randomDir = random::uniform() * 2.0f - 1.0f;
            float combinedDirection = shDirection + randomDir;
            float current = morphers[idx].originalValue;
            float baseRange = 0.5f;  // 參數範圍 0-1
            float delta = combinedDirection * baseRange * morphScale;
            morphers[idx++].targetValue = clamp(current + delta, 0.0f, 1.0f);
        }
    }

    void applyMorphing(float progress) {
        int idx = 0;
        if (morphTargetEqLow) {
            params[EQ_LOW_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetEqMid) {
            params[EQ_MID_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetEqHigh) {
            params[EQ_HIGH_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetThreshold) {
            params[THRESHOLD_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetMinSlice) {
            params[THRESHOLD_CV_ATTEN_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetScan) {
            params[SCAN_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetFeedback) {
            params[FEEDBACK_AMOUNT_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetShSlew) {
            params[SH_SLEW_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetShAmount) {
            params[SH_AMOUNT_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetShRate) {
            params[SH_RATE_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
        if (morphTargetSpeed) {
            params[SPEED_PARAM].setValue(morphers[idx].interpolate(
                morphers[idx].originalValue, morphers[idx].targetValue, progress, morphCurve));
            idx++;
        }
    }

    void restoreOriginalParameters() {
        int idx = 0;
        if (morphTargetEqLow) params[EQ_LOW_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetEqMid) params[EQ_MID_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetEqHigh) params[EQ_HIGH_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetThreshold) params[THRESHOLD_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetMinSlice) params[THRESHOLD_CV_ATTEN_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetScan) params[SCAN_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetFeedback) params[FEEDBACK_AMOUNT_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetShSlew) params[SH_SLEW_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetShAmount) params[SH_AMOUNT_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetShRate) params[SH_RATE_PARAM].setValue(morphers[idx++].originalValue);
        if (morphTargetSpeed) params[SPEED_PARAM].setValue(morphers[idx++].originalValue);
    }

    // 重新掃描切片：當 threshold 改變時重新偵測所有切片
    void rescanSlices() {
        if (layer.recordedLength <= 0) return;

        // 清除舊切片
        numSlices = 0;

        // 使用當前 threshold 重新掃描整個 buffer
        float threshold = smoothedThreshold.value;
        float minSliceTime = params[THRESHOLD_CV_ATTEN_PARAM].getValue(); // 最小切片時間（秒）
        int minSliceSamples = (int)(minSliceTime * 48000.0f); // 假設 48kHz
        float lastAmp = 0.0f;

        for (int pos = 0; pos < layer.recordedLength; pos++) {
            // 使用混合訊號
            float mixedSample = (layer.bufferL[pos] + layer.bufferR[pos]) * 0.5f;
            float currentAmp = std::abs(mixedSample);

            // 偵測從低音量到高音量的突變（attack）
            if (lastAmp < threshold && currentAmp >= threshold) {
                // 結束上一個切片
                if (numSlices > 0 && slices[numSlices - 1].active) {
                    slices[numSlices - 1].endSample = pos - 1;
                }

                // 開始新切片
                if (numSlices < MAX_SLICES) {
                    Slice newSlice;
                    newSlice.startSample = pos;
                    newSlice.active = true;
                    newSlice.peakAmplitude = 0.0f;
                    slices[numSlices++] = newSlice;
                }
            }

            // 更新當前切片的 peak amplitude
            if (numSlices > 0 && slices[numSlices - 1].active) {
                slices[numSlices - 1].peakAmplitude = std::max(
                    slices[numSlices - 1].peakAmplitude, currentAmp);
            }

            lastAmp = currentAmp;
        }

        // 結束最後一個切片
        if (numSlices > 0 && slices[numSlices - 1].active) {
            slices[numSlices - 1].endSample = layer.recordedLength - 1;
        }

        // 過濾掉太短的切片（in-place filtering）
        int writeIdx = 0;
        for (int readIdx = 0; readIdx < numSlices; readIdx++) {
            int sliceLength = slices[readIdx].endSample - slices[readIdx].startSample;
            if (sliceLength >= minSliceSamples) {
                if (writeIdx != readIdx) {
                    slices[writeIdx] = slices[readIdx];
                }
                writeIdx++;
            }
        }
        numSlices = writeIdx;
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
json_object_set_new(rootJ, "morphAmount", json_real(morphAmount));

        // Morph Target 開關
        json_object_set_new(rootJ, "morphTargetEqLow", json_boolean(morphTargetEqLow));
        json_object_set_new(rootJ, "morphTargetEqMid", json_boolean(morphTargetEqMid));
        json_object_set_new(rootJ, "morphTargetEqHigh", json_boolean(morphTargetEqHigh));
        json_object_set_new(rootJ, "morphTargetThreshold", json_boolean(morphTargetThreshold));
        json_object_set_new(rootJ, "morphTargetMinSlice", json_boolean(morphTargetMinSlice));
        json_object_set_new(rootJ, "morphTargetScan", json_boolean(morphTargetScan));
        json_object_set_new(rootJ, "morphTargetFeedback", json_boolean(morphTargetFeedback));
        json_object_set_new(rootJ, "morphTargetShSlew", json_boolean(morphTargetShSlew));
        json_object_set_new(rootJ, "morphTargetShAmount", json_boolean(morphTargetShAmount));
        json_object_set_new(rootJ, "morphTargetShRate", json_boolean(morphTargetShRate));
        json_object_set_new(rootJ, "morphTargetSpeed", json_boolean(morphTargetSpeed));

        // 保存 buffer 資料與 slices
        if (layer.recordedLength > 0) {
            // Save recorded length
            json_object_set_new(rootJ, "recordedLength", json_integer(layer.recordedLength));

            // Save playback state
            json_object_set_new(rootJ, "playbackPosition", json_integer(layer.playbackPosition));
            json_object_set_new(rootJ, "currentSliceIndex", json_integer(layer.currentSliceIndex));
            json_object_set_new(rootJ, "isPlaying", json_boolean(isPlaying));
            json_object_set_new(rootJ, "isLooping", json_boolean(isLooping));
            json_object_set_new(rootJ, "isRecording", json_boolean(isRecording));
            json_object_set_new(rootJ, "recordPosition", json_integer(recordPosition));

            // Save buffer data using base64 encoding
            // Convert float arrays to bytes for efficient storage
            size_t bufferBytes = layer.recordedLength * sizeof(float);

            // Left channel
            std::string base64L = rack::string::toBase64(
                (const uint8_t*)layer.bufferL,
                bufferBytes
            );
            json_object_set_new(rootJ, "bufferL", json_string(base64L.c_str()));

            // Right channel
            std::string base64R = rack::string::toBase64(
                (const uint8_t*)layer.bufferR,
                bufferBytes
            );
            json_object_set_new(rootJ, "bufferR", json_string(base64R.c_str()));

            // Save slices (only save valid slices, not entire fixed array)
            json_t* slicesJ = json_array();
            for (int i = 0; i < numSlices; i++) {
                json_t* sliceJ = json_object();
                json_object_set_new(sliceJ, "startSample", json_integer(slices[i].startSample));
                json_object_set_new(sliceJ, "endSample", json_integer(slices[i].endSample));
                json_object_set_new(sliceJ, "peakAmplitude", json_real(slices[i].peakAmplitude));
                json_object_set_new(sliceJ, "active", json_boolean(slices[i].active));
                json_array_append_new(slicesJ, sliceJ);
            }
            json_object_set_new(rootJ, "slices", slicesJ);
        }

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
json_t* morphAmountJ = json_object_get(rootJ, "morphAmount");
        if (morphAmountJ) {
            morphAmount = json_real_value(morphAmountJ);
        }

        // 載入 Morph Target 開關
        json_t* morphTargetEqLowJ = json_object_get(rootJ, "morphTargetEqLow");
        if (morphTargetEqLowJ) morphTargetEqLow = json_boolean_value(morphTargetEqLowJ);

        json_t* morphTargetEqMidJ = json_object_get(rootJ, "morphTargetEqMid");
        if (morphTargetEqMidJ) morphTargetEqMid = json_boolean_value(morphTargetEqMidJ);

        json_t* morphTargetEqHighJ = json_object_get(rootJ, "morphTargetEqHigh");
        if (morphTargetEqHighJ) morphTargetEqHigh = json_boolean_value(morphTargetEqHighJ);

        json_t* morphTargetThresholdJ = json_object_get(rootJ, "morphTargetThreshold");
        if (morphTargetThresholdJ) morphTargetThreshold = json_boolean_value(morphTargetThresholdJ);

        json_t* morphTargetMinSliceJ = json_object_get(rootJ, "morphTargetMinSlice");
        if (morphTargetMinSliceJ) morphTargetMinSlice = json_boolean_value(morphTargetMinSliceJ);

        json_t* morphTargetScanJ = json_object_get(rootJ, "morphTargetScan");
        if (morphTargetScanJ) morphTargetScan = json_boolean_value(morphTargetScanJ);

        json_t* morphTargetFeedbackJ = json_object_get(rootJ, "morphTargetFeedback");
        if (morphTargetFeedbackJ) morphTargetFeedback = json_boolean_value(morphTargetFeedbackJ);

        json_t* morphTargetShSlewJ = json_object_get(rootJ, "morphTargetShSlew");
        if (morphTargetShSlewJ) morphTargetShSlew = json_boolean_value(morphTargetShSlewJ);

        json_t* morphTargetShAmountJ = json_object_get(rootJ, "morphTargetShAmount");
        if (morphTargetShAmountJ) morphTargetShAmount = json_boolean_value(morphTargetShAmountJ);

        json_t* morphTargetShRateJ = json_object_get(rootJ, "morphTargetShRate");
        if (morphTargetShRateJ) morphTargetShRate = json_boolean_value(morphTargetShRateJ);

        json_t* morphTargetSpeedJ = json_object_get(rootJ, "morphTargetSpeed");
        if (morphTargetSpeedJ) morphTargetSpeed = json_boolean_value(morphTargetSpeedJ);

        // 載入 buffer 資料
        json_t* recordedLengthJ = json_object_get(rootJ, "recordedLength");
        if (recordedLengthJ) {
            int savedLength = json_integer_value(recordedLengthJ);

            if (savedLength > 0 && savedLength <= MAX_BUFFER_SIZE) {
                layer.recordedLength = savedLength;

                // Restore playback state
                json_t* playbackPosJ = json_object_get(rootJ, "playbackPosition");
                if (playbackPosJ) layer.playbackPosition = json_integer_value(playbackPosJ);

                json_t* sliceIndexJ = json_object_get(rootJ, "currentSliceIndex");
                if (sliceIndexJ) layer.currentSliceIndex = json_integer_value(sliceIndexJ);

                json_t* isPlayingJ = json_object_get(rootJ, "isPlaying");
                if (isPlayingJ) isPlaying = json_boolean_value(isPlayingJ);

                json_t* isLoopingJ = json_object_get(rootJ, "isLooping");
                if (isLoopingJ) isLooping = json_boolean_value(isLoopingJ);

                json_t* isRecordingJ = json_object_get(rootJ, "isRecording");
                if (isRecordingJ) isRecording = json_boolean_value(isRecordingJ);

                json_t* recordPosJ = json_object_get(rootJ, "recordPosition");
                if (recordPosJ) recordPosition = json_integer_value(recordPosJ);

                // Restore buffer data from base64
                json_t* bufferLJ = json_object_get(rootJ, "bufferL");
                json_t* bufferRJ = json_object_get(rootJ, "bufferR");

                if (bufferLJ && bufferRJ) {
                    const char* base64L = json_string_value(bufferLJ);
                    const char* base64R = json_string_value(bufferRJ);

                    std::vector<uint8_t> bytesL = rack::string::fromBase64(base64L);
                    std::vector<uint8_t> bytesR = rack::string::fromBase64(base64R);

                    size_t expectedBytes = savedLength * sizeof(float);

                    if (bytesL.size() == expectedBytes && bytesR.size() == expectedBytes) {
                        // Copy decoded bytes back to float arrays
                        std::memcpy(layer.bufferL, bytesL.data(), expectedBytes);
                        std::memcpy(layer.bufferR, bytesR.data(), expectedBytes);
                    }
                }

                // Restore slices
                json_t* slicesJ = json_object_get(rootJ, "slices");
                if (slicesJ && json_is_array(slicesJ)) {
                    numSlices = 0;
                    size_t sliceCount = json_array_size(slicesJ);

                    for (size_t i = 0; i < sliceCount && numSlices < MAX_SLICES; i++) {
                        json_t* sliceJ = json_array_get(slicesJ, i);
                        Slice slice;

                        json_t* startJ = json_object_get(sliceJ, "startSample");
                        if (startJ) slice.startSample = json_integer_value(startJ);

                        json_t* endJ = json_object_get(sliceJ, "endSample");
                        if (endJ) slice.endSample = json_integer_value(endJ);

                        json_t* peakJ = json_object_get(sliceJ, "peakAmplitude");
                        if (peakJ) slice.peakAmplitude = json_real_value(peakJ);

                        json_t* activeJ = json_object_get(sliceJ, "active");
                        if (activeJ) slice.active = json_boolean_value(activeJ);

                        slices[numSlices++] = slice;
                    }
                }
            }
        }
    }

    // 儲存 WAV 檔案 (使用 VCV Rack 內建的 system API)
    void saveWave(std::string path) {
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file) {
            WARN("Could not save WAV file: %s", path.c_str());
            return;
        }

        // 取得錄音長度
        int maxLength = layer.recordedLength;

        if (maxLength == 0) {
            std::fclose(file);
            WARN("No audio recorded to save");
            return;
        }

        // WAV file header (44 bytes for PCM)
        uint32_t sampleRate = 48000;
        uint16_t numChannels = 2;
        uint16_t bitsPerSample = 16;
        uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
        uint16_t blockAlign = numChannels * bitsPerSample / 8;
        uint32_t dataSize = maxLength * numChannels * bitsPerSample / 8;
        uint32_t fileSize = 36 + dataSize;

        // RIFF header
        std::fwrite("RIFF", 1, 4, file);
        std::fwrite(&fileSize, 4, 1, file);
        std::fwrite("WAVE", 1, 4, file);

        // fmt chunk
        std::fwrite("fmt ", 1, 4, file);
        uint32_t fmtSize = 16;
        std::fwrite(&fmtSize, 4, 1, file);
        uint16_t audioFormat = 1; // PCM
        std::fwrite(&audioFormat, 2, 1, file);
        std::fwrite(&numChannels, 2, 1, file);
        std::fwrite(&sampleRate, 4, 1, file);
        std::fwrite(&byteRate, 4, 1, file);
        std::fwrite(&blockAlign, 2, 1, file);
        std::fwrite(&bitsPerSample, 2, 1, file);

        // data chunk
        std::fwrite("data", 1, 4, file);
        std::fwrite(&dataSize, 4, 1, file);

        // Write audio data (interleaved stereo, 16-bit PCM)
        for (int i = 0; i < maxLength; i++) {
            float mixL = layer.bufferL[i];
            float mixR = layer.bufferR[i];

            // Clamp and convert to 16-bit PCM (從 ±10V 縮放到 ±1.0)
            int16_t sampleL = (int16_t)clamp((mixL / 10.0f) * 32767.0f, -32768.0f, 32767.0f);
            int16_t sampleR = (int16_t)clamp((mixR / 10.0f) * 32767.0f, -32768.0f, 32767.0f);

            std::fwrite(&sampleL, 2, 1, file);
            std::fwrite(&sampleR, 2, 1, file);
        }

        std::fclose(file);
        INFO("Saved WAV file: %s (%d frames)", path.c_str(), maxLength);
    }

    // 載入 WAV 檔案 (簡單的 PCM 讀取器)
    void loadWave(std::string path) {
        INFO("Loading WAV file: %s", path.c_str());
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) {
            WARN("Could not load WAV file: %s", path.c_str());
            return;
        }

        // Read RIFF header
        char riff[4];
        uint32_t fileSize;
        char wave[4];
        std::fread(riff, 1, 4, file);
        std::fread(&fileSize, 4, 1, file);
        std::fread(wave, 1, 4, file);

        if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) {
            std::fclose(file);
            WARN("Invalid WAV file: %s", path.c_str());
            return;
        }

        // Find fmt and data chunks
        uint16_t numChannels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;
        uint32_t dataSize = 0;
        long dataPos = 0;

        while (!std::feof(file)) {
            char chunkId[4];
            uint32_t chunkSize;

            if (std::fread(chunkId, 1, 4, file) != 4) break;
            if (std::fread(&chunkSize, 4, 1, file) != 1) break;

            if (std::memcmp(chunkId, "fmt ", 4) == 0) {
                uint16_t audioFormat;
                std::fread(&audioFormat, 2, 1, file);
                std::fread(&numChannels, 2, 1, file);
                std::fread(&sampleRate, 4, 1, file);
                std::fseek(file, 6, SEEK_CUR); // skip byteRate and blockAlign
                std::fread(&bitsPerSample, 2, 1, file);
                std::fseek(file, chunkSize - 16, SEEK_CUR); // skip any extra fmt data
            } else if (std::memcmp(chunkId, "data", 4) == 0) {
                dataSize = chunkSize;
                dataPos = std::ftell(file);
                break;
            } else {
                std::fseek(file, chunkSize, SEEK_CUR); // skip unknown chunk
            }
        }

        if (dataSize == 0 || dataPos == 0) {
            std::fclose(file);
            WARN("No audio data found in WAV file: %s", path.c_str());
            return;
        }

        std::fseek(file, dataPos, SEEK_SET);

        // 清除當前錄音層
        layer.clear();

        int bytesPerSample = bitsPerSample / 8;
        int numFrames = dataSize / (numChannels * bytesPerSample);
        int framesToCopy = std::min(numFrames, MAX_BUFFER_SIZE);

        INFO("WAV info: bits=%d bytes=%d frames=%d toCopy=%d channels=%d",
             bitsPerSample, bytesPerSample, numFrames, framesToCopy, numChannels);

        for (int i = 0; i < framesToCopy; i++) {
            float sampleL = 0.0f;
            float sampleR = 0.0f;

            if (bitsPerSample == 16) {
                int16_t sample16;
                std::fread(&sample16, 2, 1, file);
                sampleL = (sample16 / 32768.0f) * 10.0f;

                if (numChannels >= 2) {
                    std::fread(&sample16, 2, 1, file);
                    sampleR = (sample16 / 32768.0f) * 10.0f;
                    std::fseek(file, (numChannels - 2) * 2, SEEK_CUR);
                } else {
                    sampleR = sampleL;
                }
            } else if (bitsPerSample == 24) {
                // 24-bit samples (3 bytes, little-endian)
                uint8_t bytes[3];
                std::fread(bytes, 3, 1, file);
                int32_t sample24 = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
                if (sample24 & 0x800000) sample24 |= 0xFF000000;  // sign extend
                sampleL = (sample24 / 8388608.0f) * 10.0f;

                if (numChannels >= 2) {
                    std::fread(bytes, 3, 1, file);
                    sample24 = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
                    if (sample24 & 0x800000) sample24 |= 0xFF000000;
                    sampleR = (sample24 / 8388608.0f) * 10.0f;
                    std::fseek(file, (numChannels - 2) * 3, SEEK_CUR);
                } else {
                    sampleR = sampleL;
                }
            } else {
                // Skip unsupported formats
                std::fseek(file, numChannels * bytesPerSample, SEEK_CUR);
                continue;
            }

            layer.bufferL[i] = sampleL;
            layer.bufferR[i] = sampleR;
        }

        layer.recordedLength = framesToCopy;
        layer.playbackPosition = 0;
        layer.active = true;  // 確保 layer 啟用
        layer.currentSliceIndex = 0;

        // 創建一個覆蓋整個樣本的切片
        numSlices = 0;
        Slice initialSlice;
        initialSlice.startSample = 0;
        initialSlice.endSample = framesToCopy;
        initialSlice.active = true;
        initialSlice.peakAmplitude = 0.0f;
        slices[0] = initialSlice;
        numSlices = 1;

        // 掃描找出峰值振幅
        for (int i = 0; i < framesToCopy; i++) {
            float amp = std::max(std::abs(layer.bufferL[i]), std::abs(layer.bufferR[i]));
            if (amp > slices[0].peakAmplitude) {
                slices[0].peakAmplitude = amp;
            }
        }

        // 重設 loop end 到最大
        params[LOOP_END_PARAM].setValue(1.0f);
        smoothedLoopEnd.reset(1.0f);

        // 重設可能造成雜音的參數
        params[SPEED_PARAM].setValue(0.5f);  // 正常1x速度 (旋鈕中間位置)
        params[FEEDBACK_AMOUNT_PARAM].setValue(0.0f);
        smoothedFeedbackAmount.reset(0.0f);

        // 開始播放
        isPlaying = true;

        std::fclose(file);
        INFO("Loaded WAV file: %s (%d frames, %d channels, %d Hz, peak: %.2fV)",
             path.c_str(), framesToCopy, numChannels, sampleRate, slices[0].peakAmplitude);
        INFO("Layer state: active=%d, recordedLength=%d",
             layer.active, layer.recordedLength);

        // Rescan slices with current threshold to apply slice detection immediately
        rescanSlices();
    }
};

// MetaModule: WaveformDisplay, UnderlineWidget, GreenBlueLight, SpeedPolyCVLine removed (custom widgets not supported)
// ============================================================================
// Widget
// ============================================================================

struct WeiiiDocumentaWidget : ModuleWidget {
WeiiiDocumentaWidget(WeiiiDocumenta* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "WeiiiDocumenta.png")));
box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        // MetaModule: WhiteBottomPanel and WaveformDisplay removed (custom widgets not supported)

        // ========== 控制區 (3 列佈局，優化垂直間距) ==========
        // 波形顯示底部在 Y=65，加 12px 間距 = Y=77

        // 3列 X 座標 (往右移60px)
        float col1 = 80;   // 左列
        float col2 = 120;  // 中列
        float col3 = 160;  // 右列

        // 按鈕區水平平均分布 12HP = 180px 三組平均分配
        float btnSpacing = box.size.x / 3.0f;  // 每組60px
        float btn1X = btnSpacing * 0.5f;       // 30px
        float btn2X = btnSpacing * 1.5f;       // 90px
        float btn3X = btnSpacing * 2.5f;       // 150px

addParam(createParamCentered<VCVButton>(Vec(btn1X-11, 110), module, WeiiiDocumenta::REC_BUTTON_PARAM));
        addChild(createLightCentered<MediumLight<RedLight>>(Vec(btn1X-11, 110), module, WeiiiDocumenta::REC_LIGHT));
        addInput(createInputCentered<PJ301MPort>(Vec(btn1X+13, 110), module, WeiiiDocumenta::REC_TRIGGER_INPUT));

        // PLAY/LOOP (MetaModule: UnderlineWidget removed)
        addParam(createParamCentered<VCVButton>(Vec(btn2X-11, 110), module, WeiiiDocumenta::PLAY_BUTTON_PARAM));
        addChild(createLightCentered<MediumLight<GreenRedLight>>(Vec(btn2X-11, 110), module, WeiiiDocumenta::PLAY_LIGHT));
        addInput(createInputCentered<PJ301MPort>(Vec(btn2X+13, 110), module, WeiiiDocumenta::PLAY_TRIGGER_INPUT));

addParam(createParamCentered<VCVButton>(Vec(btn3X-11, 110), module, WeiiiDocumenta::CLEAR_BUTTON_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(btn3X+13, 110), module, WeiiiDocumenta::CLEAR_TRIGGER_INPUT));

        // EQ旋鈕 X=30 (從上到下: HIGH, MID, LOW)
addParam(createParamCentered<RoundBlackKnob>(Vec(30, 155), module, WeiiiDocumenta::EQ_HIGH_PARAM));

addParam(createParamCentered<RoundBlackKnob>(Vec(30, 205), module, WeiiiDocumenta::EQ_MID_PARAM));

addParam(createParamCentered<RoundBlackKnob>(Vec(30, 255), module, WeiiiDocumenta::EQ_LOW_PARAM));

        // Send/Return 接口 (在 EQ 下方) - 左邊 Send, 右邊 Return
        float sendX = 15.0f;
        float returnX = 45.0f;
        float srY1 = 290.0f;  // L (往上 5px)
        float srY2 = 315.0f;  // R

addOutput(createOutputCentered<PJ301MPort>(Vec(sendX, srY1), module, WeiiiDocumenta::SEND_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(sendX, srY2), module, WeiiiDocumenta::SEND_R_OUTPUT));

addInput(createInputCentered<PJ301MPort>(Vec(returnX, srY1), module, WeiiiDocumenta::RETURN_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(returnX, srY2), module, WeiiiDocumenta::RETURN_R_INPUT));

        // Row 1: THRSH / SCAN / FDBK (往下 Y+20)
        // THRESH (RoundBlackKnob - 主要參數)
addParam(createParamCentered<RoundBlackKnob>(Vec(col1, 149), module, WeiiiDocumenta::THRESHOLD_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col1, 176), module, WeiiiDocumenta::THRESHOLD_CV_INPUT));
addParam(createParamCentered<Trimpot>(Vec(col1, 205), module, WeiiiDocumenta::THRESHOLD_CV_ATTEN_PARAM));

        // SCAN (GrayKnob - 手動切片瀏覽)
addParam(createParamCentered<RoundSmallBlackKnob>(Vec(col2, 149), module, WeiiiDocumenta::SCAN_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col2, 176), module, WeiiiDocumenta::SCAN_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(col2, 199), module, WeiiiDocumenta::SCAN_CV_ATTEN_PARAM));

        // FEEDBACK (GrayKnob - 輔助參數)
addParam(createParamCentered<RoundSmallBlackKnob>(Vec(col3, 149), module, WeiiiDocumenta::FEEDBACK_AMOUNT_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col3, 176), module, WeiiiDocumenta::FEEDBACK_AMOUNT_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(col3, 199), module, WeiiiDocumenta::FEEDBACK_AMOUNT_CV_ATTEN_PARAM));

        // CHAOS
addParam(createParamCentered<VCVButton>(Vec(col2, 222), module, WeiiiDocumenta::MORPH_BUTTON_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col3, 222), module, WeiiiDocumenta::MORPH_TRIGGER_INPUT));

        // Row 2: SLEW / AMT / RATE (S&H 區塊 - 原 Chaos)
        // SLEW (只有旋鈕，無 CV input)
addParam(createParamCentered<RoundSmallBlackKnob>(Vec(col1, 260), module, WeiiiDocumenta::SH_SLEW_PARAM));

        // AMT (RoundBlackKnob - S&H 輸出大小)
addParam(createParamCentered<RoundBlackKnob>(Vec(col2, 262), module, WeiiiDocumenta::SH_AMOUNT_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col2, 290), module, WeiiiDocumenta::SH_AMOUNT_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(col2, 313), module, WeiiiDocumenta::SH_AMOUNT_CV_ATTEN_PARAM));

        // RATE (GrayKnob - S&H 速度)
addParam(createParamCentered<RoundSmallBlackKnob>(Vec(col3, 260), module, WeiiiDocumenta::SH_RATE_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(col3, 290), module, WeiiiDocumenta::SH_RATE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(Vec(col3, 313), module, WeiiiDocumenta::SH_RATE_CV_ATTEN_PARAM));

        // S&H CV Output (取代原本的 Chaos 輸出)
addOutput(createOutputCentered<PJ301MPort>(Vec(col1, 305), module, WeiiiDocumenta::SH_CV_OUTPUT));

        // ========== I/O 區域 ==========
        // 白色背景從 Y=330 開始

        // 第一行 Y=343: I/L
addInput(createInputCentered<PJ301MPort>(Vec(24, 343), module, WeiiiDocumenta::AUDIO_INPUT_L));

        // 第二行 Y=368: I/R
addInput(createInputCentered<PJ301MPort>(Vec(24, 368), module, WeiiiDocumenta::AUDIO_INPUT_R));

        // MetaModule: SpeedPolyCVLine removed (custom widget not supported)

        // Speed 控制
addParam(createParamCentered<RoundSmallBlackKnob>(Vec(55, 354), module, WeiiiDocumenta::SPEED_PARAM));

        // Polyphonic 控制 (X=120 同 S&H Amount CV Input)
addParam(createParamCentered<RoundSmallBlackKnob>(Vec(120, 354), module, WeiiiDocumenta::POLY_PARAM));

        // CV inputs 垂直排列在兩旋鈕中間 (X=88), Y對齊 I/L O/L (343) 和 I/R O/R (368)
        addInput(createInputCentered<PJ301MPort>(Vec(88, 343), module, WeiiiDocumenta::SPEED_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(Vec(88, 368), module, WeiiiDocumenta::POLY_CV_INPUT));

        // O/L
addOutput(createOutputCentered<PJ301MPort>(Vec(160, 343), module, WeiiiDocumenta::MAIN_OUTPUT_L));

        // O/R
addOutput(createOutputCentered<PJ301MPort>(Vec(160, 368), module, WeiiiDocumenta::MAIN_OUTPUT_R));
    }
void appendContextMenu(ui::Menu* menu) override {
        WeiiiDocumenta* module = dynamic_cast<WeiiiDocumenta*>(this->module);
        if (!module) return;

        // MetaModule: Load/Save WAV disabled (no osdialog support)
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Morph Time"));

        struct MorphTimeSlider : ui::Slider {
            struct MorphTimeQuantity : Quantity {
                WeiiiDocumenta* module;
                MorphTimeQuantity(WeiiiDocumenta* module) : module(module) {}

                void setValue(float value) override {
                    if (module) {
                        // Map 0-1 to 0-20 seconds linearly
                        module->morphTime = clamp(value * 20.0f, 0.0f, 20.0f);
                    }
                }

                float getValue() override {
                    if (module) {
                        // Reverse mapping
                        return module->morphTime / 20.0f;
                    }
                    return 0.25f;  // Default position for 5s
                }

                float getMinValue() override { return 0.0f; }
                float getMaxValue() override { return 1.0f; }
                float getDefaultValue() override { return 0.25f; }  // Default for 5s
                std::string getLabel() override { return "Morph Time"; }
                std::string getUnit() override { return " s"; }
                std::string getDisplayValueString() override {
                    if (module) {
                        return string::f("%.1f", module->morphTime);
                    }
                    return "5.0";
                }
            };

            MorphTimeSlider(WeiiiDocumenta* module) {
                box.size.x = 200.0f;
                quantity = new MorphTimeQuantity(module);
            }

            ~MorphTimeSlider() {
                delete quantity;
            }
        };

        MorphTimeSlider* morphSlider = new MorphTimeSlider(module);
        morphSlider->box.size.x = 200.0f;
        menu->addChild(morphSlider);

        // Morph Amount
        menu->addChild(createMenuLabel("Morph Amount"));

        struct MorphAmountSlider : ui::Slider {
            struct MorphAmountQuantity : Quantity {
                WeiiiDocumenta* module;
                MorphAmountQuantity(WeiiiDocumenta* module) : module(module) {}

                void setValue(float value) override {
                    if (module) {
                        // Map 0-1 to 0-5x linearly
                        module->morphAmount = clamp(value * 5.0f, 0.0f, 5.0f);
                    }
                }

                float getValue() override {
                    if (module) {
                        return module->morphAmount / 5.0f;
                    }
                    return 0.2f;  // Default position for 1x
                }

                float getMinValue() override { return 0.0f; }
                float getMaxValue() override { return 1.0f; }
                float getDefaultValue() override { return 0.2f; }  // Default for 1x
                std::string getLabel() override { return "Morph Amount"; }
                std::string getUnit() override { return " x"; }
                std::string getDisplayValueString() override {
                    if (module) {
                        return string::f("%.1f", module->morphAmount);
                    }
                    return "1.0";
                }
            };

            MorphAmountSlider(WeiiiDocumenta* module) {
                box.size.x = 200.0f;
                quantity = new MorphAmountQuantity(module);
            }

            ~MorphAmountSlider() {
                delete quantity;
            }
        };

        MorphAmountSlider* morphAmountSlider = new MorphAmountSlider(module);
        morphAmountSlider->box.size.x = 200.0f;
        menu->addChild(morphAmountSlider);

        // Morph Targets 子選單
        menu->addChild(createMenuLabel("Morph Targets"));

        struct MorphTargetItem : MenuItem {
            WeiiiDocumenta* module;
            bool* targetFlag;

            void onButton(const event::Button& e) override {
                if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
                    if (module && targetFlag) {
                        *targetFlag = !(*targetFlag);
                    }
                    e.consume(this);
                }
            }

            void step() override {
                if (module && targetFlag) {
                    rightText = *targetFlag ? "✔" : "";
                }
                MenuItem::step();
            }
        };

        auto createMorphTargetItem = [&](const std::string& label, bool* flag) {
            MorphTargetItem* item = new MorphTargetItem;
            item->text = label;
            item->module = module;
            item->targetFlag = flag;
            return item;
        };

        menu->addChild(createMorphTargetItem("EQ Low", &module->morphTargetEqLow));
        menu->addChild(createMorphTargetItem("EQ Mid", &module->morphTargetEqMid));
        menu->addChild(createMorphTargetItem("EQ High", &module->morphTargetEqHigh));
        menu->addChild(createMorphTargetItem("Threshold", &module->morphTargetThreshold));
        menu->addChild(createMorphTargetItem("Min Slice Time", &module->morphTargetMinSlice));
        menu->addChild(createMorphTargetItem("Scan", &module->morphTargetScan));
        menu->addChild(createMorphTargetItem("Feedback", &module->morphTargetFeedback));
        menu->addChild(createMorphTargetItem("S&H Slew", &module->morphTargetShSlew));
        menu->addChild(createMorphTargetItem("S&H Amount", &module->morphTargetShAmount));
        menu->addChild(createMorphTargetItem("S&H Rate", &module->morphTargetShRate));
        menu->addChild(createMorphTargetItem("Speed", &module->morphTargetSpeed));

        // Panel Theme at the bottom
        menu->addChild(new MenuSeparator);
}
};

Model* modelWeiiiDocumenta = createModel<WeiiiDocumenta, WeiiiDocumentaWidget>("WeiiiDocumenta");
