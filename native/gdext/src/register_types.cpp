#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include "dc_core.h"
#include "dc_models.h"
#include "dc_character.h"
#include "dc_dungeon.h"
#include "dc_walker.h"
#include "dc_town.h"
#include "dc_townwalk.h"
#include "dc_sound.h"
#include "dc_opening.h"

using namespace godot;

static void initialize_dc_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(DcCore);
    GDREGISTER_CLASS(DcModels);
    GDREGISTER_CLASS(DcCharacter);
    GDREGISTER_CLASS(DcDungeon);
    GDREGISTER_CLASS(DcWalker);
    GDREGISTER_CLASS(DcTown);
    GDREGISTER_CLASS(DcTownWalk);
    GDREGISTER_CLASS(DcSound);
    GDREGISTER_CLASS(DcOpening);
}
static void uninitialize_dc_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

extern "C" {
GDExtensionBool GDE_EXPORT dc_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_dc_module);
    init_obj.register_terminator(uninitialize_dc_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
