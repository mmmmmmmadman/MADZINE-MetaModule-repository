#include "plugin.hpp"

struct Quantizer : Module {
    enum ParamIds {
        SCALE_PARAM,
        OFFSET_PARAM,
        C_MICROTUNE_PARAM,
        CS_MICROTUNE_PARAM,
        D_MICROTUNE_PARAM,
        DS_MICROTUNE_PARAM,
        E_MICROTUNE_PARAM,
        F_MICROTUNE_PARAM,
        FS_MICROTUNE_PARAM,
        G_MICROTUNE_PARAM,
        GS_MICROTUNE_PARAM,
        A_MICROTUNE_PARAM,
        AS_MICROTUNE_PARAM,
        B_MICROTUNE_PARAM,
        NOTE_C_PARAM,
        NOTE_CS_PARAM,
        NOTE_D_PARAM,
        NOTE_DS_PARAM,
        NOTE_E_PARAM,
        NOTE_F_PARAM,
        NOTE_FS_PARAM,
        NOTE_G_PARAM,
        NOTE_GS_PARAM,
        NOTE_A_PARAM,
        NOTE_AS_PARAM,
        NOTE_B_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        PITCH_INPUT,
        PITCH_INPUT_2,
        PITCH_INPUT_3,
        OFFSET_CV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        PITCH_OUTPUT,
        PITCH_OUTPUT_2,
        PITCH_OUTPUT_3,
        NUM_OUTPUTS
    };
    enum LightIds {
        NOTE_C_LIGHT,
        NOTE_CS_LIGHT,
        NOTE_D_LIGHT,
        NOTE_DS_LIGHT,
        NOTE_E_LIGHT,
        NOTE_F_LIGHT,
        NOTE_FS_LIGHT,
        NOTE_G_LIGHT,
        NOTE_GS_LIGHT,
        NOTE_A_LIGHT,
        NOTE_AS_LIGHT,
        NOTE_B_LIGHT,
        NUM_LIGHTS
    };

    bool enabledNotes[12];
    int ranges[24];
    bool playingNotes[12];
    
    static const float EQUAL_TEMPERAMENT[12];
    static const float JUST_INTONATION[12];
    static const float PYTHAGOREAN[12];
    static const float ARABIC_MAQAM[12];
    static const float INDIAN_RAGA[12];
    static const float GAMELAN_PELOG[12];
    static const float JAPANESE_GAGAKU[12];
    static const float TURKISH_MAKAM[12];
    static const float PERSIAN_DASTGAH[12];
    static const float QUARTER_TONE[12];
    
    int currentPreset = 0;

    Quantizer() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(SCALE_PARAM, 0.0f, 2.0f, 1.0f, "Scale", "%", 0.f, 100.f);
        configParam(OFFSET_PARAM, -1.f, 1.f, 0.f, "Pre-offset", " semitones", 0.f, 12.f);
        
        std::string noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        for (int i = 0; i < 12; i++) {
            configParam(C_MICROTUNE_PARAM + i, -50.f, 50.f, 0.f, noteNames[i] + " Microtune", " cents");
            configParam(NOTE_C_PARAM + i, 0.0f, 1.0f, 1.0f, noteNames[i] + " Enable");
        }
        
        configInput(PITCH_INPUT, "CV1");
        configInput(PITCH_INPUT_2, "CV2");
        configInput(PITCH_INPUT_3, "CV3");
        configInput(OFFSET_CV_INPUT, "Offset CV");
        configOutput(PITCH_OUTPUT, "Pitch");
        configOutput(PITCH_OUTPUT_2, "Pitch 2");
        configOutput(PITCH_OUTPUT_3, "Pitch 3");
        
        for (int i = 0; i < 12; i++) {
            configLight(NOTE_C_LIGHT + i, noteNames[i] + " Light");
        }
        
        configBypass(PITCH_INPUT, PITCH_OUTPUT);
        onReset();
    }

    void onReset() override {
        for (int i = 0; i < 12; i++) {
            enabledNotes[i] = true;
            params[NOTE_C_PARAM + i].setValue(1.0f);
        }
        updateRanges();
    }

    void onRandomize() override {
        for (int i = 0; i < 12; i++) {
            enabledNotes[i] = (random::uniform() < 0.5f);
            params[NOTE_C_PARAM + i].setValue(enabledNotes[i] ? 1.0f : 0.0f);
        }
        updateRanges();
    }

    void process(const ProcessArgs& args) override {
        bool playingNotes[12] = {};
        float scaleParam = params[SCALE_PARAM].getValue();
        float offsetParam = params[OFFSET_PARAM].getValue();
        
        for (int i = 0; i < 12; i++) {
            enabledNotes[i] = params[NOTE_C_PARAM + i].getValue() > 0.5f;
        }
        updateRanges();
        
        if (inputs[OFFSET_CV_INPUT].isConnected()) {
            offsetParam += inputs[OFFSET_CV_INPUT].getVoltage();
        }

        for (int track = 0; track < 3; track++) {
            int inputId = PITCH_INPUT + track;
            int outputId = PITCH_OUTPUT + track;
            
            int channels = std::max(inputs[inputId].getChannels(), 1);
            
            for (int c = 0; c < channels; c++) {
                float pitch = inputs[inputId].getVoltage(c);
                
                pitch *= scaleParam;
                pitch += offsetParam;
                
                int range = std::floor(pitch * 24);
                int octave = eucDiv(range, 24);
                range -= octave * 24;
                int quantizedNote = ranges[range] + octave * 12;
                int noteInOctave = eucMod(quantizedNote, 12);
                playingNotes[noteInOctave] = true;
                
                float microtuneOffset = params[C_MICROTUNE_PARAM + noteInOctave].getValue() / 1200.f;
                pitch = float(quantizedNote) / 12.f + microtuneOffset;
                
                outputs[outputId].setVoltage(pitch, c);
            }
            outputs[outputId].setChannels(channels);
        }
        
        for (int i = 0; i < 12; i++) {
            this->playingNotes[i] = playingNotes[i];
            lights[NOTE_C_LIGHT + i].setBrightness(playingNotes[i] ? 1.0f : (enabledNotes[i] ? 0.3f : 0.0f));
        }
    }
    
    void updateRanges() {
        bool anyEnabled = false;
        for (int note = 0; note < 12; note++) {
            if (enabledNotes[note]) {
                anyEnabled = true;
                break;
            }
        }
        
        for (int i = 0; i < 24; i++) {
            int closestNote = 0;
            int closestDist = INT_MAX;
            for (int note = -12; note <= 24; note++) {
                int dist = std::abs((i + 1) / 2 - note);
                if (anyEnabled && !enabledNotes[eucMod(note, 12)]) {
                    continue;
                }
                if (dist < closestDist) {
                    closestNote = note;
                    closestDist = dist;
                }
                else {
                    break;
                }
            }
            ranges[i] = closestNote;
        }
    }

    void applyMicrotunePreset(int presetIndex) {
        const float* preset = nullptr;
        
        switch (presetIndex) {
            case 0: preset = EQUAL_TEMPERAMENT; break;
            case 1: preset = JUST_INTONATION; break;
            case 2: preset = PYTHAGOREAN; break;
            case 3: preset = ARABIC_MAQAM; break;
            case 4: preset = INDIAN_RAGA; break;
            case 5: preset = GAMELAN_PELOG; break;
            case 6: preset = JAPANESE_GAGAKU; break;
            case 7: preset = TURKISH_MAKAM; break;
            case 8: preset = PERSIAN_DASTGAH; break;
            case 9: preset = QUARTER_TONE; break;
        }
        
        if (preset) {
            for (int i = 0; i < 12; i++) {
                params[C_MICROTUNE_PARAM + i].setValue(preset[i]);
            }
        }
    }
    
    void applyScalePreset(int scaleIndex) {
        static const bool SCALE_PRESETS[16][12] = {
            {true, true, true, true, true, true, true, true, true, true, true, true},
            {true, false, true, false, true, true, false, true, false, true, false, true},
            {true, false, true, true, false, true, false, true, true, false, true, false},
            {true, false, true, false, true, false, false, true, false, true, false, true},
            {true, false, false, true, false, true, false, true, false, false, true, false},
            {true, false, true, true, false, true, false, true, false, true, true, false},
            {true, true, false, true, false, true, false, true, true, false, true, false},
            {true, false, true, false, true, false, true, true, false, true, false, true},
            {true, false, true, false, true, true, false, true, false, true, true, false},
            {true, true, false, true, false, true, true, false, true, false, true, false},
            {true, false, false, false, true, false, false, true, false, false, false, false},
            {true, false, false, true, false, false, false, true, false, false, false, false},
            {true, false, true, true, false, true, false, true, true, false, true, true},
            {true, true, false, true, true, false, true, true, true, false, true, true},
            {true, false, true, true, true, false, true, true, false, true, true, false},
            {true, false, true, false, true, true, true, true, false, true, false, true}
        };
        
        if (scaleIndex >= 0 && scaleIndex < 16) {
            for (int i = 0; i < 12; i++) {
                enabledNotes[i] = SCALE_PRESETS[scaleIndex][i];
                params[NOTE_C_PARAM + i].setValue(enabledNotes[i] ? 1.0f : 0.0f);
            }
            updateRanges();
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();

        json_t* enabledNotesJ = json_array();
        for (int i = 0; i < 12; i++) {
            json_array_insert_new(enabledNotesJ, i, json_boolean(enabledNotes[i]));
        }
        json_object_set_new(rootJ, "enabledNotes", enabledNotesJ);
        json_object_set_new(rootJ, "currentPreset", json_integer(currentPreset));

        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* enabledNotesJ = json_object_get(rootJ, "enabledNotes");
        if (enabledNotesJ) {
            for (int i = 0; i < 12; i++) {
                json_t* enabledNoteJ = json_array_get(enabledNotesJ, i);
                if (enabledNoteJ) {
                    enabledNotes[i] = json_boolean_value(enabledNoteJ);
                    params[NOTE_C_PARAM + i].setValue(enabledNotes[i] ? 1.0f : 0.0f);
                }
            }
        }
        
        json_t* presetJ = json_object_get(rootJ, "currentPreset");
        if (presetJ) {
            currentPreset = json_integer_value(presetJ);
        }
        
        updateRanges();
    }
};

const float Quantizer::EQUAL_TEMPERAMENT[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const float Quantizer::JUST_INTONATION[12] = {0, -29.3, -3.9, 15.6, -13.7, -2.0, -31.3, 2.0, -27.4, -15.6, 17.6, -11.7};
const float Quantizer::PYTHAGOREAN[12] = {0, -90.2, 3.9, -5.9, 7.8, -2.0, -92.2, 2.0, -88.3, 5.9, -3.9, 9.8};
const float Quantizer::ARABIC_MAQAM[12] = {0, 0, -50, 0, 0, 0, 0, 0, -50, 0, -50, 0};
const float Quantizer::INDIAN_RAGA[12] = {0, 22, -28, 22, -28, 0, 22, 0, 22, -28, 22, -28};
const float Quantizer::GAMELAN_PELOG[12] = {0, 0, 40, 0, -20, 20, 0, 0, 40, 0, -20, 20};
const float Quantizer::JAPANESE_GAGAKU[12] = {0, 0, -14, 0, 16, 0, 0, 0, -14, 16, 0, 16};
const float Quantizer::TURKISH_MAKAM[12] = {0, 24, -24, 24, 0, 24, -24, 0, 24, -24, 24, 0};
const float Quantizer::PERSIAN_DASTGAH[12] = {0, 0, -34, 0, 16, 0, 0, 0, -34, 16, 0, 16};
const float Quantizer::QUARTER_TONE[12] = {0, 50, 0, 50, 0, 0, 50, 0, 50, 0, 50, 0};

struct QuantizerWidget : ModuleWidget {
    QuantizerWidget(Quantizer* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "Quantizer.png")));

        box.size = Vec(4 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        addParam(createParamCentered<RoundBlackKnob>(Vec(46, 55), module, Quantizer::SCALE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(46, 100), module, Quantizer::OFFSET_PARAM));
        addInput(createInputCentered<PJ301MPort>(Vec(46, 140), module, Quantizer::OFFSET_CV_INPUT));

        std::string noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        
        int leftPositions[5] = {1, 3, 6, 8, 10};
        int rightPositions[7] = {0, 2, 4, 5, 7, 9, 11};
        
        Vec leftCoords[5] = {
            Vec(15, 310),
            Vec(15, 285),
            Vec(15, 235),
            Vec(15, 210),
            Vec(15, 185)
        };
        
        Vec rightCoords[7] = {
            Vec(45, 320),
            Vec(45, 295),
            Vec(45, 270),
            Vec(45, 245),
            Vec(45, 220),
            Vec(45, 195),
            Vec(45, 170)
        };

        for (int i = 0; i < 5; i++) {
            int noteIndex = leftPositions[i];
            addParam(createParamCentered<Trimpot>(leftCoords[i], module, Quantizer::C_MICROTUNE_PARAM + noteIndex));
            addParam(createParamCentered<VCVButton>(Vec(leftCoords[i].x - 8, leftCoords[i].y - 8), module, Quantizer::NOTE_C_PARAM + noteIndex));
            addChild(createLightCentered<SmallLight<RedLight>>(Vec(leftCoords[i].x - 8, leftCoords[i].y + 8), module, Quantizer::NOTE_C_LIGHT + noteIndex));
        }
        
        for (int i = 0; i < 7; i++) {
            int noteIndex = rightPositions[i];
            addParam(createParamCentered<Trimpot>(rightCoords[i], module, Quantizer::C_MICROTUNE_PARAM + noteIndex));
            addParam(createParamCentered<VCVButton>(Vec(rightCoords[i].x - 8, rightCoords[i].y - 8), module, Quantizer::NOTE_C_PARAM + noteIndex));
            addChild(createLightCentered<SmallLight<RedLight>>(Vec(rightCoords[i].x - 8, rightCoords[i].y + 8), module, Quantizer::NOTE_C_LIGHT + noteIndex));
        }

        addInput(createInputCentered<PJ301MPort>(Vec(15, 340), module, Quantizer::PITCH_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(45, 340), module, Quantizer::PITCH_OUTPUT));
        
        addInput(createInputCentered<PJ301MPort>(Vec(15, 358), module, Quantizer::PITCH_INPUT_2));
        addOutput(createOutputCentered<PJ301MPort>(Vec(45, 358), module, Quantizer::PITCH_OUTPUT_2));
        
        addInput(createInputCentered<PJ301MPort>(Vec(15, 374), module, Quantizer::PITCH_INPUT_3));
        addOutput(createOutputCentered<PJ301MPort>(Vec(45, 374), module, Quantizer::PITCH_OUTPUT_3));
    }

    void appendContextMenu(Menu* menu) override {
        Quantizer* module = getModule<Quantizer>();
        if (!module) return;

        menu->addChild(new MenuSeparator);
        
        menu->addChild(createSubmenuItem("Scale Presets", "", [=](Menu* menu) {
            std::string scaleNames[16] = {
                "Chromatic", "Major (Ionian)", "Minor (Aeolian)", "Pentatonic Major",
                "Pentatonic Minor", "Dorian", "Phrygian", "Lydian",
                "Mixolydian", "Locrian", "Major Triad", "Minor Triad",
                "Blues", "Arabic", "Japanese", "Whole Tone"
            };
            
            for (int i = 0; i < 16; i++) {
                menu->addChild(createMenuItem(scaleNames[i], "", [=]() {
                    module->applyScalePreset(i);
                }));
            }
        }));
        
        menu->addChild(createSubmenuItem("Microtune Presets", "", [=](Menu* menu) {
            std::string presetNames[10] = {
                "Equal Temperament", "Just Intonation", "Pythagorean", "Arabic Maqam",
                "Indian Raga", "Gamelan Pelog", "Japanese Gagaku", "Turkish Makam",
                "Persian Dastgah", "Quarter-tone"
            };
            
            for (int i = 0; i < 10; i++) {
                menu->addChild(createMenuItem(presetNames[i], "", [=]() {
                    module->applyMicrotunePreset(i);
                    module->currentPreset = i;
                }));
            }
        }));
    }
};

Model* modelQuantizer = createModel<Quantizer, QuantizerWidget>("Quantizer");