#include "plugin.hpp"

#ifdef METAMODULE_BUILTIN
extern Plugin* pluginInstance;
void init_MADZINE(Plugin* p) {
#else
Plugin* pluginInstance;
void init(Plugin* p) {
#endif
    pluginInstance = p;
    p->addModel(modelSwingLFO);
    p->addModel(modelEuclideanRhythm);
    p->addModel(modelADGenerator);
    p->addModel(modelPinpple);
    p->addModel(modelMADDY);
    p->addModel(modelPPaTTTerning);
    p->addModel(modelTWNC);
    p->addModel(modelTWNCLight);
    p->addModel(modelQQ);
    p->addModel(modelObserver);
    p->addModel(modelTWNC2);
    p->addModel(modelU8);
    p->addModel(modelYAMANOTE);
    p->addModel(modelObserfour);
    p->addModel(modelKIMO);
    p->addModel(modelQuantizer);
    p->addModel(modelEllenRipley);
    p->addModel(modelMADDYPlus);
    p->addModel(modelEnvVCA6);
    p->addModel(modelNIGOQ);
    p->addModel(modelRunshow);
    p->addModel(modelDECAPyramid);
    p->addModel(modelKEN);
    p->addModel(modelLaunchpad);
    p->addModel(modelPyramid);
    p->addModel(modelSongMode);
    p->addModel(modelUniversalRhythm);
    p->addModel(modelWeiiiDocumenta);
}