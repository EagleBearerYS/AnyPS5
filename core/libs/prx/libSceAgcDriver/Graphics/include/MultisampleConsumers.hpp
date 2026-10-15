#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_MULTISAMPLECONSUMERS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_MULTISAMPLECONSUMERS_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include "Recompiler.hpp"

namespace AgcDriver::Graphics {

bool TraceMultisampleConsumers();
void NoteMultisampleDraw(const Context& context, const State& state);
void NoteMultisampleRead(const GuestTextureResource& resource);
void NotePixelSampleUse(const State& state, const ShaderRecompiler::ShaderPixelStageInfo& pixel);
void NoteMultisampleResolve(const ColorMetadataPass& pass);

}

#endif
