#include "plugin.hpp"
#include <chrono>

static const int BUFFER_SIZE = 256;

struct Runshow : Module {
    enum ParamId {
        RESET_PARAM,
        START_STOP_PARAM,
        TIMER_30MIN_PARAM,      // 30分鐘計時器間隔控制
        TIMER_15MIN_PARAM,      // 15分鐘計時器間隔控制
        BAR_1_PARAM,            // 第1小節控制
        BAR_2_PARAM,            // 第2小節控制
        BAR_3_PARAM,            // 第3小節控制
        BAR_4_PARAM,            // 第4小節控制
        PARAMS_LEN
    };
    enum InputId {
        CLOCK_INPUT,
        RESET_INPUT,
        START_STOP_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        TIMER_30MIN_OUTPUT,     // 30分鐘計時器觸發
        TIMER_15MIN_OUTPUT,     // 15分鐘計時器觸發
        BAR_1_OUTPUT,           // 第1小節觸發
        BAR_2_OUTPUT,           // 第2小節觸發
        BAR_3_OUTPUT,           // 第3小節觸發
        BAR_4_OUTPUT,           // 第4小節觸發
        OUTPUTS_LEN
    };
    enum LightId {
        BEAT_LIGHT,
        LIGHTS_LEN
    };

    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger resetTrigger;
    dsp::SchmittTrigger startStopTrigger;
    dsp::SchmittTrigger resetButtonTrigger;
    dsp::SchmittTrigger startStopButtonTrigger;

    bool running = false;
    int clockCount = 0;
    int currentBar = 0;
    int quarter_notes = 0;
    int eighth_notes = 0;
    int sixteenth_notes = 0;

    std::chrono::steady_clock::time_point startTime;
    float elapsedSeconds = 0.f;
    float lastClockTime = 0.f;
    int lastBarInCycle = -1;

    dsp::PulseGenerator timer30MinPulse;
    dsp::PulseGenerator timer15MinPulse;
    dsp::PulseGenerator barPulses[4];

    // Simple timing variables
    float last30MinTrigger = 0.f;
    float last15MinTrigger = 0.f;

    Runshow() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configButton(RESET_PARAM, "Reset");
        configButton(START_STOP_PARAM, "Start/Stop");

        configParam(TIMER_30MIN_PARAM, 1.0f, 10.0f, 5.0f, "30min Timer Interval", " min");
        configParam(TIMER_15MIN_PARAM, 0.25f, 5.0f, 1.0f, "15min Timer Interval", " min");
        configParam(BAR_1_PARAM, 1.0f, 32.0f, 4.0f, "Bar 1 Length", " beats");
        configParam(BAR_2_PARAM, 1.0f, 32.0f, 4.0f, "Bar 2 Length", " beats");
        configParam(BAR_3_PARAM, 1.0f, 32.0f, 4.0f, "Bar 3 Length", " beats");
        configParam(BAR_4_PARAM, 1.0f, 32.0f, 4.0f, "Bar 4 Length", " beats");

        // Snap to integer values for bar parameters
        getParamQuantity(BAR_1_PARAM)->snapEnabled = true;
        getParamQuantity(BAR_2_PARAM)->snapEnabled = true;
        getParamQuantity(BAR_3_PARAM)->snapEnabled = true;
        getParamQuantity(BAR_4_PARAM)->snapEnabled = true;

        configInput(CLOCK_INPUT, "Clock");
        configInput(RESET_INPUT, "Reset");
        configInput(START_STOP_INPUT, "Start/Stop");

        configOutput(TIMER_30MIN_OUTPUT, "30min Timer");
        configOutput(TIMER_15MIN_OUTPUT, "15min Timer");
        configOutput(BAR_1_OUTPUT, "Bar 1");
        configOutput(BAR_2_OUTPUT, "Bar 2");
        configOutput(BAR_3_OUTPUT, "Bar 3");
        configOutput(BAR_4_OUTPUT, "Bar 4");

        configLight(BEAT_LIGHT, "Beat");

        // Initialize timing
        startTime = std::chrono::steady_clock::now();
    }

    void process(const ProcessArgs& args) override {
        bool resetTriggered = resetTrigger.process(inputs[RESET_INPUT].getVoltage()) ||
                              resetButtonTrigger.process(params[RESET_PARAM].getValue());
        bool startStopTriggered = startStopTrigger.process(inputs[START_STOP_INPUT].getVoltage()) ||
                                  startStopButtonTrigger.process(params[START_STOP_PARAM].getValue());

        if (startStopTriggered) {
            running = !running;
            if (running) {
                startTime = std::chrono::steady_clock::now();
            }
        }

        if (resetTriggered) {
            clockCount = 0;
            currentBar = 0;
            quarter_notes = 0;
            eighth_notes = 0;
            sixteenth_notes = 0;
            elapsedSeconds = 0.f;
            last30MinTrigger = 0.f;
            last15MinTrigger = 0.f;
            lastBarInCycle = -1;
            startTime = std::chrono::steady_clock::now();
            lights[BEAT_LIGHT].setBrightness(0.f);
        }

        if (running) {
            auto currentTime = std::chrono::steady_clock::now();
            elapsedSeconds = std::chrono::duration<float>(currentTime - startTime).count();

            if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
                clockCount++;
                sixteenth_notes++;

                // Every 2 sixteenth notes = 1 eighth note
                if (clockCount % 2 == 0) {
                    eighth_notes++;
                }

                // Every 4 sixteenth notes = 1 quarter note (beat)
                if (clockCount % 4 == 0) {
                    quarter_notes++;
                    lights[BEAT_LIGHT].setBrightness(1.f);
                }

                // Calculate bar transitions
                int bar1Length = (int)params[BAR_1_PARAM].getValue() * 4; // Convert beats to sixteenth notes
                int bar2Length = (int)params[BAR_2_PARAM].getValue() * 4;
                int bar3Length = (int)params[BAR_3_PARAM].getValue() * 4;
                int bar4Length = (int)params[BAR_4_PARAM].getValue() * 4;
                int totalCycleClock = bar1Length + bar2Length + bar3Length + bar4Length;

                int clocksInCycle = clockCount % totalCycleClock;
                int currentBarInCycle = 0;

                // Determine current bar
                if (clocksInCycle < bar1Length) {
                    currentBarInCycle = 0;
                } else if (clocksInCycle < bar1Length + bar2Length) {
                    currentBarInCycle = 1;
                } else if (clocksInCycle < bar1Length + bar2Length + bar3Length) {
                    currentBarInCycle = 2;
                } else {
                    currentBarInCycle = 3;
                }

                // Trigger bar outputs when entering new bar
                if (currentBarInCycle != lastBarInCycle) {
                    if (currentBarInCycle == 0) barPulses[0].trigger(1e-3f);
                    else if (currentBarInCycle == 1) barPulses[1].trigger(1e-3f);
                    else if (currentBarInCycle == 2) barPulses[2].trigger(1e-3f);
                    else if (currentBarInCycle == 3) barPulses[3].trigger(1e-3f);
                    lastBarInCycle = currentBarInCycle;
                }
            }

            // Handle timer outputs
            float timer30Interval = params[TIMER_30MIN_PARAM].getValue() * 60.0f; // Convert minutes to seconds
            float timer15Interval = params[TIMER_15MIN_PARAM].getValue() * 60.0f;

            if (elapsedSeconds - last30MinTrigger >= timer30Interval) {
                timer30MinPulse.trigger(1e-3f);
                last30MinTrigger = elapsedSeconds;
            }

            if (elapsedSeconds - last15MinTrigger >= timer15Interval) {
                timer15MinPulse.trigger(1e-3f);
                last15MinTrigger = elapsedSeconds;
            }
        }

        // Dim beat light
        lights[BEAT_LIGHT].setBrightness(lights[BEAT_LIGHT].getBrightness() * 0.99f);

        // Process pulse generators
        bool timer30Active = timer30MinPulse.process(args.sampleTime);
        bool timer15Active = timer15MinPulse.process(args.sampleTime);

        outputs[TIMER_30MIN_OUTPUT].setVoltage(timer30Active ? 10.0f : 0.0f);
        outputs[TIMER_15MIN_OUTPUT].setVoltage(timer15Active ? 10.0f : 0.0f);

        for (int i = 0; i < 4; i++) {
            bool barActive = barPulses[i].process(args.sampleTime);
            outputs[BAR_1_OUTPUT + i].setVoltage(barActive ? 10.0f : 0.0f);
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "running", json_boolean(running));
        json_object_set_new(rootJ, "clockCount", json_integer(clockCount));
        json_object_set_new(rootJ, "elapsedSeconds", json_real(elapsedSeconds));
        json_object_set_new(rootJ, "quarter_notes", json_integer(quarter_notes));
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* runningJ = json_object_get(rootJ, "running");
        if (runningJ) running = json_boolean_value(runningJ);

        json_t* clockCountJ = json_object_get(rootJ, "clockCount");
        if (clockCountJ) clockCount = json_integer_value(clockCountJ);

        json_t* elapsedSecondsJ = json_object_get(rootJ, "elapsedSeconds");
        if (elapsedSecondsJ) elapsedSeconds = json_real_value(elapsedSecondsJ);

        json_t* quarterNotesJ = json_object_get(rootJ, "quarter_notes");
        if (quarterNotesJ) quarter_notes = json_integer_value(quarterNotesJ);

        // Recalculate start time based on elapsed seconds
        auto now = std::chrono::steady_clock::now();
        startTime = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(elapsedSeconds));
    }
};

// Internal widget for time code - uses Widget instead of LedDisplay
struct TimeCodeWidget : widget::Widget {
    Runshow* module;

    TimeCodeWidget() {
        box.size = Vec(70, 40);
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(20, 20, 20));
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgStrokeColor(args.vg, nvgRGB(60, 60, 60));
        nvgStrokeWidth(args.vg, 1.0);
        nvgStroke(args.vg);

        if (!module) return;

        int minutes = (int)(module->elapsedSeconds / 60.0f) % 1000;
        int seconds = (int)module->elapsedSeconds % 60;
        int milliseconds = (int)((module->elapsedSeconds - floor(module->elapsedSeconds)) * 100);
        char timeString[32];
        snprintf(timeString, 32, "%d:%02d:%02d", minutes, seconds, milliseconds);

        int bar = module->currentBar + 1;
        int beat = (module->clockCount / 4) % 4 + 1;
        int tick = module->clockCount % 4 + 1;
        char barString[32];
        snprintf(barString, 32, "%03d:%d:%d", bar, beat, tick);

        nvgFontSize(args.vg, 10);
        nvgFontFaceId(args.vg, APP->window->uiFont->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        nvgFillColor(args.vg, nvgRGB(0, 255, 100));
        nvgText(args.vg, box.size.x / 2, box.size.y * 0.35f, timeString, NULL);

        nvgFillColor(args.vg, nvgRGB(255, 200, 0));
        nvgText(args.vg, box.size.x / 2, box.size.y * 0.7f, barString, NULL);
    }
};

// Internal widget for progress bars - uses Widget instead of LedDisplay
struct ProgressBarsWidget : widget::Widget {
    Runshow* module;

    ProgressBarsWidget() {
        box.size = Vec(150, 180);
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(20, 20, 20));
        nvgFill(args.vg);

        if (!module) return;

        float barWidth = 12;
        float barSpacing = 4;
        int numBars = 6;
        float totalWidth = numBars * barWidth + (numBars - 1) * barSpacing;
        float startX = (box.size.x - totalWidth) / 2;

        for (int i = 0; i < numBars; i++) {
            float x = startX + i * (barWidth + barSpacing);

            nvgBeginPath(args.vg);
            nvgRect(args.vg, x, 0, barWidth, box.size.y);
            nvgStrokeColor(args.vg, nvgRGB(60, 60, 60));
            nvgStrokeWidth(args.vg, 1.0);
            nvgStroke(args.vg);

            float fillHeight = 0;

            if (i == 0) {
                float totalMinutes = module->elapsedSeconds / 60.0f;
                float timer30Interval = module->params[Runshow::TIMER_30MIN_PARAM].getValue();
                float fiveMinuteBlocks = totalMinutes / timer30Interval;
                fillHeight = box.size.y * std::min(fiveMinuteBlocks / 6.0f, 1.0f);
            } else if (i == 1) {
                float totalMinutes = module->elapsedSeconds / 60.0f;
                float timer15Interval = module->params[Runshow::TIMER_15MIN_PARAM].getValue();
                fillHeight = box.size.y * std::min(totalMinutes / (timer15Interval * 15.0f), 1.0f);
            } else {
                int barIndex = i - 2;
                int bar1Length = (int)module->params[Runshow::BAR_1_PARAM].getValue() * 4;
                int bar2Length = (int)module->params[Runshow::BAR_2_PARAM].getValue() * 4;
                int bar3Length = (int)module->params[Runshow::BAR_3_PARAM].getValue() * 4;
                int bar4Length = (int)module->params[Runshow::BAR_4_PARAM].getValue() * 4;

                int barLengths[4] = {bar1Length, bar2Length, bar3Length, bar4Length};
                int totalCycleClock = bar1Length + bar2Length + bar3Length + bar4Length;
                int clocksInCycle = module->clockCount % totalCycleClock;

                int currentBarInCycle = 0;
                int clocksInCurrentBar = clocksInCycle;
                int accum = 0;
                for (int b = 0; b < 4; b++) {
                    if (clocksInCycle < accum + barLengths[b]) {
                        currentBarInCycle = b;
                        clocksInCurrentBar = clocksInCycle - accum;
                        break;
                    }
                    accum += barLengths[b];
                }

                int barLength = barLengths[barIndex];
                if (barIndex == currentBarInCycle) {
                    float progress = (float)clocksInCurrentBar / barLength;
                    fillHeight = box.size.y * progress;
                } else if (barIndex < currentBarInCycle ||
                          (currentBarInCycle == 0 && barIndex > 0)) {
                    fillHeight = box.size.y;
                } else {
                    fillHeight = 0;
                }
            }

            if (fillHeight > 0) {
                nvgBeginPath(args.vg);
                nvgRect(args.vg, x + 1, box.size.y - fillHeight, barWidth - 2, fillHeight);
                nvgFillColor(args.vg, nvgRGB(255, 255, 255));
                nvgFill(args.vg);
            }

            nvgFontSize(args.vg, 8);
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFillColor(args.vg, nvgRGB(255, 255, 255));
            char barLabel[4];
            if (i == 0) {
                snprintf(barLabel, 4, "30m");
            } else if (i == 1) {
                snprintf(barLabel, 4, "15m");
            } else {
                snprintf(barLabel, 4, "%d", i - 1);
            }
            nvgText(args.vg, x + barWidth/2, box.size.y + 2, barLabel, NULL);
        }
    }
};

// Container Widget that holds both widgets (transparent container)
struct DisplayContainer : widget::Widget {
    Runshow* module;
    TimeCodeWidget* timeWidget;
    ProgressBarsWidget* progressWidget;

    DisplayContainer() {
        box.size = Vec(150, 246);

        timeWidget = new TimeCodeWidget();
        timeWidget->box.pos = Vec(53, 0);
        addChild(timeWidget);

        progressWidget = new ProgressBarsWidget();
        progressWidget->box.pos = Vec(0, 46);
        addChild(progressWidget);
    }

    void setModule(Runshow* m) {
        module = m;
        timeWidget->module = m;
        progressWidget->module = m;
    }
};

struct RunshowWidget : ModuleWidget {
    RunshowWidget(Runshow* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "Runshow.png")));
        box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        addParam(createParamCentered<VCVButton>(Vec(30, 70), module, Runshow::START_STOP_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(54, 70), module, Runshow::CLOCK_INPUT));
        addParam(createParamCentered<VCVButton>(Vec(152, 70), module, Runshow::RESET_PARAM));

        addInput(createInputCentered<PJ301MPort>(Vec(30, 96), module, Runshow::START_STOP_INPUT));
        addChild(createLightCentered<MediumLight<RedLight>>(Vec(54, 95), module, Runshow::BEAT_LIGHT));
        addInput(createInputCentered<PJ301MPort>(Vec(152, 96), module, Runshow::RESET_INPUT));

        DisplayContainer* display = new DisplayContainer();
        display->setModule(module);
        display->box.pos = Vec(15, 64);  // Match VCV version layout
        addChild(display);

        addParam(createParamCentered<RoundBlackKnob>(Vec(15, 343), module, Runshow::TIMER_30MIN_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(46, 343), module, Runshow::TIMER_15MIN_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(76, 343), module, Runshow::BAR_1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(107, 343), module, Runshow::BAR_2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(137, 343), module, Runshow::BAR_3_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(168, 343), module, Runshow::BAR_4_PARAM));

        addOutput(createOutputCentered<PJ301MPort>(Vec(15, 368), module, Runshow::TIMER_30MIN_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(46, 368), module, Runshow::TIMER_15MIN_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(76, 368), module, Runshow::BAR_1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(107, 368), module, Runshow::BAR_2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(137, 368), module, Runshow::BAR_3_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(168, 368), module, Runshow::BAR_4_OUTPUT));
    }
};

Model* modelRunshow = createModel<Runshow, RunshowWidget>("Runshow");