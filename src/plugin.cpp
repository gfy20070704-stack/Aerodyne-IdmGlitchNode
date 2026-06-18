#include "plugin.hpp"

Plugin* pluginInstance;

extern "C" {
#if defined(_WIN32)
__declspec(dllexport) void init(rack::Plugin* p) {
#else
void init(rack::Plugin* p) {
#endif
	pluginInstance = p;
	p->addModel(modelIdmGlitchNode);
}
}
